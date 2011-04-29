srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'
 
def set_options(opt):
  opt.tool_options('compiler_cxx')
  opt.tool_options("compiler_cc")
 
def configure(conf):
  conf.check_tool("compiler_cxx")
  conf.check_tool("compiler_cc")
  conf.check_tool("node_addon")
  #conf.env.append_value('CCFLAGS', ['-fstack-protector', '-O', '-g', '-march=native'])
  conf.env.append_value('CCFLAGS', ['-O3'])
 
def build(bld):
  upskirt = bld.new_task_gen("cc", "shlib")
  upskirt.source = """
    src/markdown.c
    src/array.c
    src/buffer.c
    src/xhtml.c """
  upskirt.includes = "src/"
  upskirt.name = "upskirt"
  upskirt.target = "upskirt"

  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.target = 'robotskirt'
  #obj.linkflags = "../lib/libupskirt.so"
  obj.includes = "upskirt"
  obj.add_objects = "upskirt"
  obj.source = 'src/robotskirt.cc'
