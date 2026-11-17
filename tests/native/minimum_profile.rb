# Build a host interpreter from the same mruby sources as PicoRuby. Keep the
# shared language extensions, without the browser's IO / kernel-ext gems.
# Hardware and the task scheduler are outside this language smoke test.
require File.join(ENV.fetch('GAME_PROJECT_ROOT'), 'tools', 'runtime_profile.rb')
require File.join(ENV.fetch('GAME_PROJECT_ROOT'), 'tools', 'build', 'vm_profile.rb')

MRuby::Build.new('minimum-profile') do |conf|
  conf.toolchain :gcc
  conf.cc.defines.concat(%w[MRB_INT64 MRB_NO_BOXING MRB_UTF8_STRING
                            MRB_CONSTRAINED_BASELINE_PROFILE=1 PICORB_VM_MRUBY])
  conf.gem core: 'mruby-bin-mruby'
  PicoRubyGaming::VMProfile.configure_method_cache(conf, ENV.fetch('GAME_METHOD_CACHE_SIZE', '0'))
  PicoRubyGaming::RuntimeProfile.add_ruby_gems(conf, MRUBY_ROOT)
  PicoRubyGaming::RuntimeProfile.add_common_gems(conf, ENV.fetch('GAME_PROJECT_ROOT'))
end
