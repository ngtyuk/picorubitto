# Host-only assembly of the existing flight example's models and transforms.
require 'digest'
module GamingCheckSource
  def self.flight
    root = File.expand_path('../..', __dir__)
    source = File.read(File.join(root, 'examples/games/3d/main.rb'))
    definitions, marker, = source.partition("\nGame.init\n")
    unless marker != '' && definitions.include?('def append_transformed_vertices(') &&
           definitions.include?('FIGHTER_VERTICES =')
      raise '3D example layout changed; update the performance fixture assembly'
    end
    definitions = definitions.sub("require 'game'\n", '')
    "module HardwareFlight\n  extend self\n  SOURCE_SHA256 = '#{Digest::SHA256.hexdigest(source)}'\n#{definitions}\nend\n"
  end
end

print GamingCheckSource.flight if $PROGRAM_NAME == __FILE__
