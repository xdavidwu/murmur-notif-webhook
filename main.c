#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>

#include "webhook.h"
#include "util.h"

struct mnw_channel {
	int32_t id;
	char *name;
};

struct mnw_player {
	uint32_t session;
	char *name;
	struct mnw_channel *channel;
};

struct mnw_server {
	int32_t id;
	sd_bus_slot *channel_created_slot, *channel_removed_slot,
		    *channel_state_changed_slot, *player_connected_slot,
		    *player_disconnected_slot, *player_state_changed_slot;
	struct list *players, *channels;
};

struct mnw_state {
	sd_bus *bus;
	sd_bus_slot *started_slot, *stopped_slot;
	struct list *servers;
	struct webhook_state *webhook;
};

struct mnw_state global_state = {0};

static const char murmur_service[] = "net.sourceforge.mumble.murmur";
static const char murmur_meta_interface[] = "net.sourceforge.mumble.Meta";
static const char murmur_server_interface[] = "net.sourceforge.mumble.Murmur";

static int handle_players(sd_bus_message *msg, struct mnw_server *server_state) {
	int res = sd_bus_message_enter_container(msg, 'a', "(ubbbbbiisii)");
	while (1) {
		res = sd_bus_message_enter_container(msg, 'r', "ubbbbbiisii");
		if (res < 0) {
			return res;
		} else if (res == 0) {
			res = sd_bus_message_exit_container(msg);
			break;
		}
		int32_t session;
		res = sd_bus_message_read(msg, "u", &session);
		if (res < 0) {
			return res;
		}
		res = sd_bus_message_skip(msg, "bbbbb");
		if (res < 0) {
			return res;
		}
		int32_t channel;
		res = sd_bus_message_read(msg, "i", &channel);
		if (res < 0) {
			return res;
		}
		res = sd_bus_message_skip(msg, "i");
		if (res < 0) {
			return res;
		}
		const char *name = NULL;
		res = sd_bus_message_read(msg, "s", &name);
		if (res < 0) {
			return res;
		}
		res = sd_bus_message_skip(msg, "ii");
		if (res < 0) {
			return res;
		}
		res = sd_bus_message_exit_container(msg);
		if (res < 0) {
			return res;
		}

		struct list *ptr = server_state->channels;
		struct mnw_channel *player_channel = NULL;
		while (ptr->next) {
			ptr = ptr->next;
			struct mnw_channel *mchannel = ptr->data;
			if (mchannel->id == channel) {
				player_channel = mchannel;
				break;
			}
		}
		assert(player_channel);

		ptr = server_state->players;
		int found = 0;
		while (ptr->next) {
			ptr = ptr->next;
			struct mnw_player *player = ptr->data;
			if (player->session == session) {
				found = 1;
				if (channel != player->channel->id) {
					char *buf = calloc(17 + strlen(name) +
						strlen(player->channel->name) +
						strlen(player_channel->name),
						sizeof(char));
					assert(buf);
					sprintf(buf, "%s moved from %s to %s",
						name, player->channel->name,
						player_channel->name);
					puts(buf);
					webhook_send(global_state.webhook, buf);
					free(buf);
					player->channel = player_channel;
				}
				break;
			}
		}
		if (!found) {
			struct mnw_player *player = calloc(1,
				sizeof(struct mnw_player));
			char *buf = calloc(9 + strlen(name) +
				strlen(player_channel->name), sizeof(char));
			assert(buf);
			sprintf(buf, "%s joined %s", name,
				player_channel->name);
			puts(buf);
			webhook_send(global_state.webhook, buf);
			free(buf);
			assert(player);
			player->session = session;
			player->name = strdup(name);
			player->channel = player_channel;
			assert(player->name);
			list_append(server_state->players, player);
		}
	}
	return res;
}

static int handle_player(sd_bus_message *msg, void *userdata, sd_bus_error *ret) {
	// only getPlayers contains (unregistered) name
	struct mnw_server *server_state = userdata;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	char path[16];
	sprintf(path, "/%d", server_state->id);
	int res = sd_bus_call_method(global_state.bus, murmur_service, path,
		murmur_server_interface, "getPlayers", &err, &msg, "");
	if (res < 0) {
		fprintf(stderr, "getPlayers failed\n");
		return res;
	}
	res = handle_players(msg, server_state);
	sd_bus_message_unref(msg);
	if (res < 0) {
		fprintf(stderr, "handle_players failed\n");
		return res;
	}
	return res;
}

static int handle_player_disconnected(sd_bus_message *msg, void *userdata,
		sd_bus_error *ret) {
	struct mnw_server *server_state = userdata;
	int res = sd_bus_message_enter_container(msg, 'r', "ubbbbbi");
	if (res <= 0) {
		return res;
	}
	uint32_t session;
	res = sd_bus_message_read(msg, "u", &session);
	if (res < 0) {
		return res;
	}

	struct list *ptr = server_state->players;
	int found = 0;
	while (ptr->next) {
		ptr = ptr->next;
		struct mnw_player *player = ptr->data;
		if (player->session == session) {
			found = 1;
			char *buf = calloc(14 + strlen(player->name),
				sizeof(char));
			sprintf(buf, "%s disconnected", player->name);
			puts(buf);
			webhook_send(global_state.webhook, buf);
			free(buf);
			free(player->name);
			free(player);
			list_remove(server_state->players, ptr);
			break;
		}
	}
	assert(found);

	res = sd_bus_message_skip(msg, "bbbbbi");
	if (res < 0) {
		return res;
	}
	res = sd_bus_message_exit_container(msg);
	if (res < 0) {
		return res;
	}
	return res;
}

static int handle_channel(sd_bus_message *msg, void *userdata, sd_bus_error *ret) {
	struct mnw_server *server_state = userdata;
	int res = sd_bus_message_enter_container(msg, 'r', "isiai");
	if (res <= 0) {
		return res;
	}
	int id;
	const char *name = NULL;
	res = sd_bus_message_read(msg, "is", &id, &name);
	if (res < 0) {
		return res;
	}

	struct list *ptr = server_state->channels;
	int found = 0;
	while (ptr->next) {
		ptr = ptr->next;
		struct mnw_channel *channel = ptr->data;
		if (channel->id == id) {
			found = 1;
			if (!strcmp(name, channel->name)) {
				printf("%d changed from %s to %s\n", id,
					channel->name, name);
				free(channel->name);
				channel->name = strdup(name);
				assert(channel->name);
			}
			break;
		}
	}
	if (!found) {
		struct mnw_channel *channel = calloc(1,
			sizeof(struct mnw_channel));
		printf("new channel %d with name %s\n", id, name);
		assert(channel);
		channel->id = id;
		channel->name = strdup(name);
		assert(channel->name);
		list_append(server_state->channels, channel);
	}

	res = sd_bus_message_skip(msg, "iai");
	if (res < 0) {
		return res;
	}
	res = sd_bus_message_exit_container(msg);
	if (res < 0) {
		return res;
	}
	return res;
}

static int handle_channel_removed(sd_bus_message *msg, void *userdata,
		sd_bus_error *ret) {
	struct mnw_server *server_state = userdata;
	int res = sd_bus_message_enter_container(msg, 'r', "isiai");
	if (res <= 0) {
		return res;
	}
	int id;
	res = sd_bus_message_read(msg, "i", &id);
	if (res < 0) {
		return res;
	}

	struct list *ptr = server_state->channels;
	int found = 0;
	while (ptr->next) {
		ptr = ptr->next;
		struct mnw_channel *channel = ptr->data;
		if (channel->id == id) {
			found = 1;
			printf("%d (%s) removed\n", id, channel->name);
			free(channel->name);
			free(channel);
			list_remove(server_state->channels, ptr);
			break;
		}
	}
	assert(found);

	res = sd_bus_message_skip(msg, "siai");
	if (res < 0) {
		return res;
	}
	res = sd_bus_message_exit_container(msg);
	if (res < 0) {
		return res;
	}
	return res;
}

static int handle_channels(sd_bus_message *msg, struct mnw_server *server_state) {
	int res = sd_bus_message_enter_container(msg, 'a', "(isiai)");
	if (res < 0) {
		return res;
	}
	while (1) {
		res = handle_channel(msg, server_state, NULL);
		if (res < 0) {
			fprintf(stderr, "handle_channel failed\n");
			return res;
		} else if (res == 0) {
			break;
		}
	}
	return res;
}

static int probe_and_listen_server(struct mnw_server *server_state) {
	int res = 0;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *msg = NULL;
	char path[16];
	sprintf(path, "/%d", server_state->id);
	res = sd_bus_call_method(global_state.bus, murmur_service, path,
		murmur_server_interface, "getChannels", &err, &msg, "");
	if (res < 0) {
		fprintf(stderr, "getChannels failed\n");
		return res;
	}
	res = handle_channels(msg, server_state);
	sd_bus_message_unref(msg);
	if (res < 0) {
		fprintf(stderr, "handle_channels failed\n");
		return res;
	}
	res = sd_bus_call_method(global_state.bus, murmur_service, path,
		murmur_server_interface, "getPlayers", &err, &msg, "");
	if (res < 0) {
		fprintf(stderr, "getPlayers failed\n");
		return res;
	}
	res = handle_players(msg, server_state);
	sd_bus_message_unref(msg);
	if (res < 0) {
		fprintf(stderr, "handle_players failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus,
		&server_state->channel_created_slot, murmur_service, path,
		murmur_server_interface, "channelCreated", handle_channel,
		server_state);
	if (res < 0) {
		fprintf(stderr, "Listen on channelCreated failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus,
		&server_state->channel_state_changed_slot, murmur_service,
		path, murmur_server_interface, "channelStateChanged",
		handle_channel, server_state);
	if (res < 0) {
		fprintf(stderr, "Listen on channelStateChanged failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus,
		&server_state->channel_removed_slot, murmur_service, path,
		murmur_server_interface, "channelRemoved",
		handle_channel_removed, server_state);
	if (res < 0) {
		fprintf(stderr, "Listen on channelRemoved failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus,
		&server_state->player_connected_slot, murmur_service, path,
		murmur_server_interface, "playerConnected",
		handle_player, server_state);
	if (res < 0) {
		fprintf(stderr, "Listen on playerConnected failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus,
		&server_state->player_connected_slot, murmur_service, path,
		murmur_server_interface, "playerStateChanged",
		handle_player, server_state);
	if (res < 0) {
		fprintf(stderr, "Listen on playerStateChanged failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus,
		&server_state->player_disconnected_slot, murmur_service, path,
		murmur_server_interface, "playerDisconnected",
		handle_player_disconnected, server_state);
	if (res < 0) {
		fprintf(stderr, "Listen on playerDisconnected failed\n");
		return res;
	}
	return res;
}

static int handle_started(sd_bus_message *msg, void *userdata, sd_bus_error *ret) {
	int32_t server_id;
	int res = sd_bus_message_read(msg, "i", &server_id);
	if (res <= 0) {
		return res;
	}
	struct mnw_server *server_state = calloc(1,
		sizeof(struct mnw_server));
	assert(server_state);
	server_state->players = list_new();
	server_state->channels = list_new();
	server_state->id = server_id;
	res = probe_and_listen_server(server_state);
	if (res < 0) {
		return res;
	}
	list_append(global_state.servers, server_state);
	return 0;
}

static void destroy_server(struct mnw_server *server_state) {
	struct list *ptr = server_state->players;
	while (ptr->next) {
		ptr = ptr->next;
		struct mnw_player *player = ptr->data;
		free(player->name);
		free(player);
	}
	list_destroy(server_state->players);
	ptr = server_state->channels;
	while (ptr->next) {
		ptr = ptr->next;
		struct mnw_channel *channel = ptr->data;
		free(channel->name);
		free(channel);
	}
	list_destroy(server_state->channels);
	sd_bus_slot_unref(server_state->player_connected_slot);
	sd_bus_slot_unref(server_state->player_disconnected_slot);
	sd_bus_slot_unref(server_state->player_state_changed_slot);
	sd_bus_slot_unref(server_state->channel_created_slot);
	sd_bus_slot_unref(server_state->channel_removed_slot);
	sd_bus_slot_unref(server_state->channel_state_changed_slot);
	free(server_state);
}

static int handle_stopped(sd_bus_message *msg, void *userdata, sd_bus_error *ret) {
	int32_t server_id;
	int res = sd_bus_message_read(msg, "i", &server_id);
	if (res <= 0) {
		return res;
	}
	struct list *ptr = global_state.servers;
	while (ptr->next) {
		ptr = ptr->next;
		struct mnw_server *server_state = ptr->data;
		if (server_state->id == server_id) {
			destroy_server(server_state);
			list_remove(global_state.servers, ptr);
			break;
		}
	}
	return 0;
}

static int handle_booted_servers(sd_bus_message *msg) {
	int res = sd_bus_message_enter_container(msg, 'a', "i");
	if (res < 0) {
		return res;
	}
	while (1) {
		res = handle_started(msg, NULL, NULL);
		if (res < 0) {
			return res;
		} else if (res == 0) {
			break;
		}
	}
	res = sd_bus_message_exit_container(msg);
	return res;
}

int main(int argc, char *argv[]) {
	assert(getenv("MNW_WEBHOOK"));
	global_state.webhook = webhook_setup();
	global_state.servers = list_new();
	int res;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *msg = NULL;
	const char *system_bus_env = getenv("MNW_USE_SYSTEM");
	if (system_bus_env && !strcmp(system_bus_env, "1")) {
		res = sd_bus_open_system(&global_state.bus);
	} else {
		res = sd_bus_open_user(&global_state.bus);
	}
	assert(res >= 0);
	res = sd_bus_call_method(global_state.bus, murmur_service, "/",
		murmur_meta_interface, "getBootedServers", &err, &msg, "");
	if (res < 0) {
		fprintf(stderr, "Initial getBootedServers failed\n");
	}
	res = handle_booted_servers(msg);
	sd_bus_message_unref(msg);
	if (res < 0) {
		fprintf(stderr, "handle_booted_servers failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus, &global_state.started_slot,
		murmur_service, "/", murmur_meta_interface, "started",
		handle_started, NULL);
	if (res < 0) {
		fprintf(stderr, "Listen on started failed\n");
		return res;
	}
	res = sd_bus_match_signal(global_state.bus, &global_state.stopped_slot,
		murmur_service, "/", murmur_meta_interface, "stopped",
		handle_stopped, NULL);
	if (res < 0) {
		fprintf(stderr, "Listen on stopped failed\n");
		return res;
	}

	do {
		sd_bus_process(global_state.bus, NULL);
	} while (sd_bus_wait(global_state.bus, UINT64_MAX) > 0);

	return 0;
}
