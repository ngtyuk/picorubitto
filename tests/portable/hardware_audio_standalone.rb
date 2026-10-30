# Bundled after hardware_audio.rb by checks --audio-only.
require 'game'
Game.init
print '[AUDIO CHECK] standalone ready', "\n"
while true
  HardwareAudio.run(:pad)
  Game::Display.clear(0xff9b)
  Game::Display.text(8, 80, 'AUDIO STOPPED', 0x4328)
  Game::Display.text(8, 112, 'A: RESTART', 0x4328)
  Game::Display.present
  while true
    Game.tick(30)
    break if Game::Input.just_pressed?(Game::Input::A)
  end
end
