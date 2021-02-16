project('murmur-notif-webhook',['c'])

systemd = dependency('libsystemd')

executable('murmur-notif-webhook', ['main.c', 'util.c'], dependencies: [systemd], install: true)
