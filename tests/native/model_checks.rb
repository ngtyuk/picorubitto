# Executed by the matching native mruby with the production Model binding.
def check(value, message)
  raise message unless value
end

def rejects(error)
  caught = false
  begin
    yield
  rescue error
    caught = true
  end
  check(caught, 'expected exception')
end

class Game::ThreeD::Model
  def reinitialize_for_test
    initialize([0,0,0],[],[])
  end
end

def exercise_model
  vertices = [-0.5, -1, 0, 1, -1, 0, 0, 1, 0]
  indices = [0, 2, 1]
  colors = [0x1234]
  edges = [0, 1, 1, 2, 2, 0]
  model = Game::ThreeD::Model.new(vertices, indices, colors, edges, 0x4321, false)
  check(ModelTest.bytes == 92, 'model storage budget')
  vertices[0] = 99
  indices[0] = 2
  colors[0] = 1
  edges[0] = 2
  # Reference uses double Ruby arithmetic, independent of the C transform.
  source = [-0.5, -1, 0, 1, -1, 0, 0, 1, 0]
  poses = [[0,0,4,0,0,0,1,1,1], [0.5,-0.2,5,0.7,-0.3,0.4,2,0.5,1.5],
           [0,0,4,0,0,0,-1,1,1], [0,0,0,0,0,0,0,0,0],
           [0,0,4,1.2,0,0,1,1,1], [0,0,4,0,0.8,0,1,1,1], [0,0,4,0,0,-0.7,1,1,1]]
  index = 0
  while index < poses.length
    t = poses[index]
    check(model.draw_instances(t).nil?, 'draw result')
    actual = ModelTest.vertices
    print 'MODEL_CASE ', t.join(','), '|', actual.join(','), "\n"
    vertex = 0
    while vertex < source.length / 3
      v = source[vertex * 3, 3]
      x,y,z = v[0]*t[6], v[1]*t[7], v[2]*t[8]
      rx = x*Math.cos(t[3])-z*Math.sin(t[3])
      rz = x*Math.sin(t[3])+z*Math.cos(t[3])
      ry = y*Math.cos(t[4])-rz*Math.sin(t[4])
      rz = y*Math.sin(t[4])+rz*Math.cos(t[4])
      expected = [t[0]+rx*Math.cos(t[5])-ry*Math.sin(t[5]), t[1]+rx*Math.sin(t[5])+ry*Math.cos(t[5]), t[2]+rz]
      expected.each_with_index { |value, axis| check((actual[vertex*3+axis]-value).abs < 0.00001, 'transform differs') }
      vertex += 1
    end
    check(ModelTest.signature == [0,2,1,0x1234,0,1,1,2,2,0,0x4321,0], 'snapshot and depth flag')
    index += 1
  end
  calls = ModelTest.calls
  check(model.draw_instances(poses[0] + poses[1]).nil?, 'multiple instances')
  check(ModelTest.calls == calls + 4, 'both geometry types per instance')
  model.draw_instances([])
  calls = ModelTest.calls
  rejects(ArgumentError) { model.draw_instances(poses[0] + [0]) }
  rejects(TypeError) { model.draw_instances(poses[0] + [0,0,4,0,0,0,'x',1,1]) }
  check(ModelTest.calls == calls, 'invalid batch drew partial output')
  rejects(ArgumentError) { model.draw_instances(Array.new(129*9, 0)) }
  rejects(ArgumentError) { model.draw_instances([0,0,4,0,0,0,32769,1,1]) }
  rejects(ArgumentError) { model.draw_instances([0,0,4,0,0,0,Float::INFINITY,1,1]) }
  rejects(TypeError) { model.draw_instances(nil) }
  rejects(TypeError) { model.dup }
  rejects(TypeError) { model.clone }
  rejects(RuntimeError) { model.reinitialize_for_test }
  Game::ThreeD.end_frame
  rejects(ArgumentError) { model.draw_instances(poses[0]) }
  check(model.draw_instances([]).nil?, 'empty batch outside a frame')
  Game::ThreeD.begin_frame
  model.draw_instances(Array.new(128) { poses[0] }.flatten)
  GC.start
  check(ModelTest.bytes == 92, 'live model was freed')
  model.draw_instances(poses[1])
  baseline = ModelTest.bytes
  1000.times { model.draw_instances(poses[0]) }
  check(ModelTest.bytes == baseline, 'steady-state model storage grew')
  true
end

Game::ThreeD.begin_frame
exercise_model
GC.start
check(ModelTest.bytes == 0, 'dead model leaked')
rejects(ArgumentError) { Game::ThreeD::Model.new([],[],[]) }
rejects(ArgumentError) { Game::ThreeD::Model.new(Array.new(257*3,0),[],[]) }
rejects(ArgumentError) { Game::ThreeD::Model.new([0,0,0],[0,1,0],[1]) }
rejects(ArgumentError) { Game::ThreeD::Model.new([0,0,0],[],[],[0]) }
rejects(ArgumentError) { Game::ThreeD::Model.new([0,0,0],[0,0,0],[65536]) }
rejects(TypeError) { Game::ThreeD::Model.new([0,0,0],[],[],[],1.5) }
rejects(TypeError) { Game::ThreeD::Model.new([0,0,0],[],[],[],1,0) }
rejects(TypeError) { Game::ThreeD::Model.new([0,0,0],[],[],[],1,nil) }
rejects(TypeError) { Game::ThreeD::Model.new([0,0,'x'],[],[]) }
rejects(TypeError) { Game::ThreeD::Model.new([0,0,0],[0.0,0,0],[1]) }
rejects(ArgumentError) { Game::ThreeD::Model.new([0,0,32769],[],[]) }
rejects(ArgumentError) { Game::ThreeD::Model.new([0,0,0],Array.new(1025*3,0),Array.new(1025,1)) }
rejects(ArgumentError) { Game::ThreeD::Model.new([0,0,0],[],[],Array.new(2049*2,0)) }
GC.start
check(ModelTest.bytes == 0, 'failed constructors leaked')
print "model native regression: PASS\n"
