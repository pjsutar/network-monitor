from conans import ConanFile, CMake

class ConanPackage(ConanFile):
    name = 'network-monitor'
    version = "0.1.0"

    generators = 'cmake_find_package'

    requires = [
        ('boost/1.74.0'),
        ("libcurl/7.87.0"),
        ("nlohmann_json/3.9.1"),
        ('openssl/1.1.1w'),
        ('spdlog/1.8.1'),
    ]

    default_options = (
        'boost:shared=False',
    )