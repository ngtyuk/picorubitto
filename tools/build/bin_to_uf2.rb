#!/usr/bin/env ruby
# frozen_string_literal: true

# Minimal UF2 writer for the RP2350 secure-Arm image. The input is the
# absolute-flash binary emitted by pico_add_bin_output.
#
# RP2350 A2's E10 workaround requires an absolute block at the start of a
# flash UF2. Every regular block also has to advertise a full 256-byte
# payload, including the final padded block.

abort "Usage: bin_to_uf2.rb INPUT.BIN OUTPUT.UF2" unless ARGV.length == 2

input_path, output_path = ARGV
payload_size = 256
block_size = 512
flash_start = 0x1000_0000
family_id = 0xe48b_ff59 # RP2350 ARM-S
absolute_family_id = 0xe48b_ff57
absolute_block_address = 0x10ff_ff00 # end of the board's 16 MB flash
magic_start_0 = 0x0a32_4655
magic_start_1 = 0x9e5d_5157
magic_end = 0x0ab1_6f30
family_id_flag = 0x0000_2000
extension_flags_flag = 0x0000_8000
ignore_block_extension = 0x9957_e304

binary = File.binread(input_path)
num_blocks = (binary.bytesize + payload_size - 1) / payload_size
abort 'input binary is empty' if num_blocks.zero?

File.open(output_path, 'wb') do |output|
  # RP2350-E10: this block is written to the end of flash before the main
  # image and prevents the boot ROM's sector bookkeeping from losing the
  # transfer. Newer boot ROMs recognize the extension and ignore the data.
  absolute_header = [magic_start_0, magic_start_1,
                     family_id_flag | extension_flags_flag,
                     absolute_block_address, payload_size, 0, 2,
                     absolute_family_id].pack('V8')
  absolute_body = ("\xef".b * payload_size) +
                  [ignore_block_extension].pack('V')
  output.write(absolute_header)
  output.write(absolute_body.ljust(476, "\0"))
  output.write([magic_end].pack('V'))

  num_blocks.times do |block_no|
    payload = binary.byteslice(block_no * payload_size, payload_size) || ''.b
    header = [magic_start_0, magic_start_1, family_id_flag,
              flash_start + block_no * payload_size, payload_size,
              block_no, num_blocks, family_id].pack('V8')
    output.write(header)
    output.write(payload.ljust(payload_size, "\0"))
    output.write("\0" * (476 - payload_size))
    output.write([magic_end].pack('V'))
  end
end

total_blocks = num_blocks + 1
abort 'UF2 block size mismatch' unless File.size(output_path) == total_blocks * block_size
puts "Wrote #{output_path} (#{total_blocks} blocks)"
