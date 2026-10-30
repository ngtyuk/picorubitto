require 'minitest/autorun'
require 'open3'

class BuildOptionsTest < Minitest::Test
  ROOT = File.expand_path('../..', __dir__)
  WORKFLOW = File.join(ROOT, 'tools/build/workflow.rb')

  def test_obsolete_experiment_switches_are_rejected
    %w[--tft-dma --tft-pipeline --tft-core1 --baseline --framebuffer-sram
       --dirty-rect --frame-diff --background-layer --optimize-for-speed
       --i2s-single-dma --i2s-wide-math --no-3d-line-clip
       --no-3d-projection-cache --no-3d-depth-sram --no-3d-model-shared
       --3d-triangle].each do |option|
      _, error, status = Open3.capture3('ruby', WORKFLOW, 'engine', option)
      refute status.success?, option
      assert_includes error, 'invalid option', option
    end
  end

  def test_board_audio_and_vm_configuration_remain_available
    output, status = Open3.capture2('ruby', WORKFLOW, 'engine', '--help')
    assert status.success?
    %w[--board --audio-output --audio-max-gain --volume-knob --i2s-bclk
       --i2s-data --vm-opt --tft-clock --profiling --firmware-only --wasm-only].each do |option|
      assert_includes output, option
    end
  end
end
