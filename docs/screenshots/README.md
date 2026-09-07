# Screenshots

**Generated, not captured.** Do not replace these by hand.

The plugin harness renders them offline, so they can be regenerated the moment
the interface changes rather than slowly drifting out of date - a screenshot of a
version nobody runs any more is worse than having none at all.

To rebuild them:

    build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-band.json --snapshot <some-temp-dir>

then copy `editor-<screen>-docs.png` over the matching file here, dropping the
`editor-` and `-docs` from the name. The sizes live in the snapshot block at the
bottom of `test/PluginHarness.cpp`, one per screen.

The same run also produces a `600x720` set and a couple of oversized ones. Those
are for the overlap checker rather than for documentation, and are not kept -
copying those in by mistake is easy, since they are the ones named without a
suffix, and it puts a cramped 600px window in the README.

These always render in the DEFAULT theme regardless of which of the eight the
owner is using. A screenshot of somebody else's colour scheme tells a reader
nothing about the plugin.
