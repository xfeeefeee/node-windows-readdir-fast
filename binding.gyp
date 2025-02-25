{
  "targets": [{
    "target_name": "node-windows-readdir-fast",
    "sources": [
      "readdirFast.cc"
    ],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")"
    ],
    'defines': [ 'NAPI_CPP_EXCEPTIONS' ],
    'conditions': [
      [ 'OS=="win"', {
        'defines': [ '_HAS_EXCEPTIONS=1' ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'ExceptionHandling': 1,
          },
        },
      }]
    ],
  }]
}
