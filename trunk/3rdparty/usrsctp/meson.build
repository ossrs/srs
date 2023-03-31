# Project definition
project('usrsctplib', 'c',
    version: '0.9.5.0',
    default_options: [
        'c_std=c99',
        'warning_level=2',
    ],
    meson_version: '>=0.49.0')

# Shared library API and ABI versions
# Notice: shared library version must be in X.Y.Z format only
soversion_full = '2.0.0'
soversion_short = '2'

# Set compiler warning flags
compiler = meson.get_compiler('c')
if compiler.get_argument_syntax() == 'msvc'
    compiler_args = compiler.get_supported_arguments([
        '/wd4100', # 'identifier' : unreferenced formal parameter
        '/wd4127', # conditional expression is constant
        '/wd4200', # nonstandard extension used : zero-sized array in struct/union
        '/wd4214', # bit field types other than int
        '/wd4706', # assignment within conditional expression
        '/wd4245', # 'conversion' : conversion from 'type1' to 'type2', signed/unsigned mismatch
        '/wd4389', # 'operator' : signed/unsigned mismatch
        '/wd4702', # unreachable code
        '/wd4701', # Potentially uninitialized local variable 'name' used
        '/wd4244', # 'conversion' conversion from 'type1' to 'type2', possible loss of data
    ])
else
    compiler_args = compiler.get_supported_arguments([
        '-pedantic',
        '-Wfloat-equal',
        '-Wshadow',
        '-Wpointer-arith',
        '-Winit-self',
        '-Wno-unused-function',
        '-Wno-unused-parameter',
        '-Wno-unreachable-code',
        '-Wstrict-prototypes',
    ])
endif
add_project_arguments(compiler_args, language: 'c')

# Configuration
compile_args = []

# Dependency: Threads
thread_dep = dependency('threads', required: true)

# Dependencies list
dependencies = [
    thread_dep,
]

# Global settings
add_project_arguments([
    '-D__Userspace__',
    '-DSCTP_SIMPLE_ALLOCATOR',
    '-DSCTP_PROCESS_LEVEL_LOCKS',
], language: 'c')

# OS-specific settings
system = host_machine.system()
if system in ['linux', 'android']
    add_project_arguments([
        '-D_GNU_SOURCE',
    ], language: 'c')
elif system == 'freebsd'
    add_project_arguments(compiler.get_supported_arguments([
            '-Wno-address-of-packed-member',
        ]), language: 'c')
elif system in ['darwin', 'ios']
    add_project_arguments([
            '-D__APPLE_USE_RFC_2292',
        ] + compiler.get_supported_arguments([
            '-Wno-address-of-packed-member',
            '-Wno-deprecated-declarations',
        ]), language: 'c')
elif system == 'qnx'
    add_project_arguments([
        '-D_QNX_SOURCE',
    ], language: 'c')
elif system == 'windows'
    dependencies += compiler.find_library('ws2_32', required: true)
    dependencies += compiler.find_library('iphlpapi', required: true)
    if compiler.get_id() == 'gcc'
        add_project_arguments(compiler.get_supported_arguments([
            '-Wno-format',
            '-D_WIN32_WINNT=0x601',  # Enables inet_ntop and friends
        ]), language: 'c')
    endif
else
    error('Unknown system: @0@'.format(system))
endif

# Feature: sys/queue
if system != 'qnx' and compiler.has_header('sys/queue.h')
    add_project_arguments('-DHAVE_SYS_QUEUE_H', language: 'c')
endif

# Feature: sys/socket, linux/ifaddr, linux/rtnetlink
if compiler.has_header('sys/socket.h')
    if compiler.has_header('linux/if_addr.h')
        add_project_arguments('-DHAVE_LINUX_IF_ADDR_H', language: 'c')
    endif

    if compiler.has_header('linux/rtnetlink.h')
        add_project_arguments('-DHAVE_LINUX_RTNETLINK_H', language: 'c')
    endif
endif

# Feature: ICMP
have_sys_types = compiler.has_header('sys/types.h')
have_netinet_in = compiler.has_header('netinet/in.h')
have_netinet_ip = compiler.has_header('netinet/ip.h')
have_netinet_ip_icmp = compiler.has_header('netinet/ip_icmp.h')
if have_sys_types and have_netinet_in and have_netinet_ip and have_netinet_ip_icmp
    add_project_arguments('-DHAVE_NETINET_IP_ICMP_H', language: 'c')
endif

# Feature: net/route
if compiler.has_header('net/route.h')
    add_project_arguments('-DHAVE_NET_ROUTE_H', language: 'c')
endif

# Feature: stdatomic
if compiler.has_header('stdatomic.h')
    add_project_arguments('-DHAVE_STDATOMIC_H', language: 'c')
endif

# Feature: sockaddr.sa_len
prefix = '''
#include <sys/types.h>
#include <sys/socket.h>
'''
have_sa_len = compiler.has_member('struct sockaddr', 'sa_len', prefix: prefix)
if have_sa_len
    add_project_arguments('-DHAVE_SA_LEN', language: 'c')
endif

# Feature: sockaddr_in.sin_len / sockaddr_in6.sin6_len / sockaddr_conn.sconn_len
prefix = '''
#include <sys/types.h>
#include <netinet/in.h>
'''
have_sin_len = compiler.has_member('struct sockaddr_in', 'sin_len', prefix: prefix)
if have_sin_len
    add_project_arguments('-DHAVE_SIN_LEN', language: 'c')
endif
have_sin6_len = compiler.has_member('struct sockaddr_in6', 'sin6_len', prefix: prefix)
if have_sin6_len
    add_project_arguments('-DHAVE_SIN6_LEN', language: 'c')
endif
have_sconn_len = compiler.has_member('struct sockaddr_conn', 'sconn_len', prefix: '#include "usrsctp.h"', include_directories: include_directories('usrsctplib'))
if have_sconn_len
    add_project_arguments('-DHAVE_SCONN_LEN', language: 'c')
endif

# Options
if get_option('sctp_invariants')
    add_project_arguments('-DINVARIANTS', language: 'c')
endif
if get_option('sctp_debug')
    add_project_arguments('-DSCTP_DEBUG', language: 'c')
    compile_args += '-DSCTP_DEBUG'
endif
if get_option('sctp_inet')
    add_project_arguments('-DINET', language: 'c')
endif
if get_option('sctp_inet6')
    add_project_arguments('-DINET6', language: 'c')
endif

# Library
subdir('usrsctplib')

# Build library
if compiler.get_id() == 'msvc' and get_option('default_library') == 'shared'
    # Needed by usrsctp_def
    find_program('dumpbin')

    usrsctp_static = static_library('usrsctp-static', sources,
        dependencies: dependencies,
        include_directories: include_dirs)

   usrsctp_def = custom_target('usrsctp.def',
        command: [find_program('gen-def.py'), '@INPUT@'],
        input: usrsctp_static,
        output: 'usrsctp.def',
        capture: true)

    usrsctp = shared_library('usrsctp',
        link_whole: usrsctp_static,
        dependencies: dependencies,
        vs_module_defs: usrsctp_def,
        install: true,
        version: meson.project_version())
else
    usrsctp = library('usrsctp', sources,
        dependencies: dependencies,
        include_directories: include_dirs,
        install: true,
        version: soversion_full,
        soversion: soversion_short)
endif

# Declare dependency
usrsctp_dep = declare_dependency(
    compile_args: compile_args,
    include_directories: include_dirs,
    link_with: usrsctp)

# Generate pkg-config file
pkg = import('pkgconfig')
pkg.generate(usrsctp,
    name: 'usrsctp',
    description: 'A portable SCTP userland stack',
    url: 'https://github.com/sctplab/usrsctp',
    extra_cflags: compile_args)

# Programs (optional)
if get_option('sctp_build_programs')
    subdir('programs')

    # Build executables
    foreach name, sources : programs
        executable(
            name,
            programs_helper_sources + sources,
            dependencies: dependencies,
            link_with: usrsctp,
            include_directories: include_dirs)
    endforeach
endif
