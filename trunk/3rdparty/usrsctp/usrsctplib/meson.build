# Includes
include_dirs = include_directories(
    '.',
    'netinet',
    'netinet6',
)

# Sources
sources = files([
    'user_environment.c',
    'user_mbuf.c',
    'user_recv_thread.c',
    'user_socket.c',
])

subdir('netinet')
subdir('netinet6')

# Install usrsctp.h
install_headers('usrsctp.h')
