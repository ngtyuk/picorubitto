# Build a host interpreter from the same mruby sources as PicoRuby. Keep the
# native language extensions, without the browser's IO / kernel-ext gems.
# Hardware and the task scheduler are outside this language smoke test.
MRuby::Build.new('minimum-profile') do |conf|
  conf.toolchain :gcc
  conf.cc.defines.concat(%w[MRB_INT64 MRB_NO_BOXING MRB_UTF8_STRING
                            MRB_CONSTRAINED_BASELINE_PROFILE=1 PICORB_VM_MRUBY])
  conf.gem core: 'mruby-bin-mruby'
  %w[mruby-math mruby-array-ext mruby-numeric-ext].each do |name|
    conf.gem core: name
  end
end
