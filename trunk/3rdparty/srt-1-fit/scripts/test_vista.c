/* Copyright © 2023 Steve Lhomme */
/* SPDX-License-Identifier: ISC */
#include <windows.h>
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600 /* _WIN32_WINNT_VISTA */
#error NOPE
#endif
int main(void)
{
    return 0;
}
