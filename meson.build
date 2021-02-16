project('murmur-notif-webhook',['c'])

sd_bus = dependency(
  'libsystemd',
  required: false,
  not_found_message: 'systemd not found, try elogind'
)

if not sd_bus.found()
  sd_bus = dependency('libelogind')
endif

curl = dependency('libcurl')

executable(
  'murmur-notif-webhook',
  ['main.c', 'webhook.c', 'util.c'],
  dependencies: [sd_bus, curl],
  install: true
)
