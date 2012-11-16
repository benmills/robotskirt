{
  'targets': [

    {
      'target_name': 'sundown',
      'type': 'static_library',
      'include_dirs': ['src'],
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
      'dependencies': ['sundown'],

      # Flags and defines
      'cflags': ['-Wall'],

      # Enable exceptions
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ],
      'conditions': [
        ['OS=="mac"', {
          'xcode_settings': {
            'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
          }
        }]
      ]
    }

  ]
}
