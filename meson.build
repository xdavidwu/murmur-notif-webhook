project('murmur-notif-webhook',['c'])

systemd = dependency('libsystemd')
curl = dependency('libcurl')

executable(
  'murmur-notif-webhook',
  ['main.c', 'webhook.c', 'util.c'],
  dependencies: [systemd, curl],
  install: true
)
