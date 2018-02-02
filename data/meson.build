data_inc = include_directories('.')

schemas = [
  'org.gnome.settings-daemon.peripherals.gschema.xml',
  'org.gnome.settings-daemon.peripherals.wacom.gschema.xml',
  'org.gnome.settings-daemon.plugins.gschema.xml',
  'org.gnome.settings-daemon.plugins.color.gschema.xml',
  'org.gnome.settings-daemon.plugins.housekeeping.gschema.xml',
  'org.gnome.settings-daemon.plugins.media-keys.gschema.xml',
  'org.gnome.settings-daemon.plugins.power.gschema.xml',
  'org.gnome.settings-daemon.plugins.sharing.gschema.xml',
  'org.gnome.settings-daemon.plugins.xsettings.gschema.xml'
]

schema_conf = configuration_data()
schema_conf.set('GETTEXT_PACKAGE', meson.project_name())

foreach schema: schemas
  configure_file(
    input: schema + '.in',
    output: schema,
    configuration: schema_conf,
    install: true,
    install_dir: gsd_schemadir
  )
endforeach

enums_header = files('gsd-enums.h')

gnome.mkenums(
  'org.gnome.settings-daemon.enums.xml',
  sources: enums_header,
  comments: '<!-- @comment@ -->',
  fhead: '<schemalist>',
  vhead: '  <@type@ id="org.gnome.settings-daemon.@EnumName@">',
  vprod: '    <value nick="@valuenick@" value="@valuenum@"/>',
  vtail: '  </@type@>',
  ftail: '</schemalist>',
  install_header: true,
  install_dir: gsd_schemadir
)

install_data(
  enums_header,
  install_dir: join_paths(gsd_pkgincludedir, meson.project_name())
)

install_data(
  'gnome-settings-daemon.convert',
  install_dir: join_paths(gsd_datadir, 'GConf', 'gsettings')
)

pkg.generate(
  version: gsd_version,
  name: meson.project_name(),
  description: meson.project_name() + ' specific enumerations',
  filebase: meson.project_name(),
  subdirs: gsd_api_name
)
