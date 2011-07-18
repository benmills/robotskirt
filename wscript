srcdir = '.'
blddir = 'build'
VERSION = '0.2.2'
 
def set_options(opt):
  opt.tool_options('compiler_cxx')
  opt.tool_options("compiler_cc")
 
def configure(conf):
  conf.check_tool("compiler_cxx")
  conf.check_tool("compiler_cc")
  conf.check_tool("node_addon")
 
def build(bld):
  sundown = bld.new_task_gen("cc", "shlib")
  sundown.source = """
    src/markdown.c
    src/array.c
    src/buffer.c
    src/html.c """
  sundown.includes = "src/"
  sundown.name = "sundown"
  sundown.target = "sundown"

  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.cxxflags = ["-g", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE", "-Wall"]
  obj.target = 'robotskirt'
  obj.add_objects = "sundown"
  obj.source = 'src/robotskirt.cc'
