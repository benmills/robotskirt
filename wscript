srcdir = '.'
blddir = 'build'
VERSION = '1.0.0'
 
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
    src/autolink.c
    src/buffer.c
    src/houdini_href_e.c
    src/houdini_html_e.c
    src/houdini_html_u.c
    src/houdini_js_e.c
    src/houdini_js_u.c
    src/houdini_uri_e.c
    src/houdini_uri_u.c
    src/houdini_xml_e.c
    src/html.c
    src/html_smartypants.c
    src/markdown.c
    src/stack.c
  """
  sundown.includes = "src/"
  sundown.name = "sundown"
  sundown.target = "sundown"

  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.cxxflags = ["-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE", "-Wall"]
  obj.target = 'robotskirt'
  obj.add_objects = "sundown"
  obj.source = 'src/robotskirt.cc'
