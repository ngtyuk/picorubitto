MRuby::Gem::Specification.new('picoruby-game') do |spec|
  spec.license = 'MIT'
  spec.author = 'PicoRuby Gaming'
  spec.summary = 'Native game console API for PicoRuby on RP2350'

  spec.cc.include_paths << File.expand_path('../../include', dir)
  spec.cc.include_paths << File.join(ENV.fetch('PICORUBY_ROOT'), 'mrbgems')
  spec.cc.defines << 'ESTALLOC_ALIGNMENT=8'
  spec.cc.flags << '-O2' if ENV['GAME_OPTIMIZE_FOR_SPEED'] == '1'
  # The backend is compiled by mruby so its public names participate in presyms.
  spec.objs << objfile(File.join(build_dir, 'src', 'mruby', 'game'))
  spec.objs << objfile(File.join(build_dir, 'src', 'mruby', 'model'))
end
