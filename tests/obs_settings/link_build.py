"""Retain the actual M2-facing store APIs, without executing them at startup."""
Import('env')
env.Append(LINKFLAGS=['-Wl,--undefined=chainpadObsSettingsLinkProbe'])
probe_env = env.Clone()
probe_env.Append(CPPPATH=[env.subst('$PROJECT_LIBDEPS_DIR/$PIOENV/ArduinoJson/src')])
probe = probe_env.BuildLibrary('$BUILD_DIR/obs-settings-link', '$PROJECT_DIR/tests/obs_settings/link')
env.Append(LIBS=[probe])
