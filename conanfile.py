from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class XrplConan(ConanFile):
    name = 'xrpl'
    version = '1.8.5'

    license = 'ISC'
    author = 'John Freeman <jfreeman@ripple.com>'
    url = 'https://github.com/ripple/rippled'

    settings = 'os', 'compiler', 'build_type', 'arch'
    options = {
        'shared': [True, False],
        'fPIC': [True, False],
    }

    default_options = {
        'shared': False,
        'fPIC': True,

        'cassandra-cpp-driver:shared': False,
        'date:header_only': True,
        'grpc:shared': False,
        'grpc:secure': True,
        'libarchive:shared': False,
        'libarchive:with_acl': False,
        'libarchive:with_bzip2': False,
        'libarchive:with_cng': False,
        'libarchive:with_expat': False,
        'libarchive:with_iconv': False,
        'libarchive:with_libxml2': False,
        'libarchive:with_lz4': True,
        'libarchive:with_lzma': False,
        'libarchive:with_lzo': False,
        'libarchive:with_nettle': False,
        'libarchive:with_openssl': False,
        'libarchive:with_pcreposix': False,
        'libarchive:with_xattr': False,
        'libarchive:with_zlib': False,
        'libpq:shared': False,
        'lz4:shared': False,
        'openssl:shared': False,
        'protobuf:shared': False,
        'protobuf:with_zlib': True,
        'rocksdb:enable_sse': False,
        'rocksdb:lite': False,
        'rocksdb:shared': False,
        'rocksdb:use_rtti': True,
        'rocksdb:with_jemalloc': False,
        'rocksdb:with_lz4': True,
        'rocksdb:with_snappy': True,
        'snappy:shared': False,
        'soci:shared': False,
        'soci:with_sqlite3': True,
        'soci:with_boost': True,
    }

    requires = [
        'doctest/2.4.6',
        'boost/1.77.0',
        'cassandra-cpp-driver/2.15.3',
        'date/3.0.1',
        'libarchive/3.6.0',
        'libpq/13.6',
        'lz4/1.9.3',
        'grpc/1.44.0',
        'nudb/2.0.8',
        'openssl/1.1.1m',
        'rocksdb/6.27.3',
        'protobuf/3.19.2',
        'snappy/1.1.9',
        'soci/4.0.3',
        'sqlite3/3.38.0',
        'zlib/1.2.11',
    ]

    generators = 'cmake_find_package'
