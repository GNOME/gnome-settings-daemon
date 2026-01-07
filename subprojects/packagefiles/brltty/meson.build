project('brltty')

# We only need the metainfo.xml file from brltty.
# Remove everything else from the dist tarball.
meson.add_dist_script('sh', '-xeu', '-c', '''
    cd "${MESON_PROJECT_DIST_ROOT}"
    find . -type f -not -name org.a11y.brltty.metainfo.xml -not -name meson.build -delete
    find . -type d -empty -delete
    rm -rfv .git
''')
