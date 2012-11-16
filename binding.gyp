{
  'target_defaults': {
    'cflags!': [ '-fno-exceptions' ],
    'cflags_cc!': [ '-fno-exceptions' ]
  },
  'targets': [
    {
      'target_name': 'sundown',
      'type': 'static_library',
      'include_dirs': ['src/'],
      'sources': [
        'src/autolink.c',
        'src/buffer.c',
        'src/houdini_href_e.c',
        'src/houdini_html_e.c',
        'src/houdini_html_u.c',
        'src/houdini_js_e.c',
        'src/houdini_js_u.c',
        'src/houdini_uri_e.c',
        'src/houdini_uri_u.c',
        'src/houdini_xml_e.c',
        'src/html.c',
        'src/html_smartypants.c',
        'src/markdown.c',
        'src/stack.c',
      ]
    },
    {
      'target_name': 'robotskirt',
      'sources': ['src/robotskirt.cc'],
      'cflags': ['-Wall'],
      'defines': [
        '_FILE_OFFSET_BITS=64',
        '_LARGEFILE_SOURCE'
      ],
      'dependencies': ['sundown']
    }
  ]
}
