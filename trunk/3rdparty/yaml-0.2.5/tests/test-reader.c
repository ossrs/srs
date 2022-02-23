#include <yaml.h>

YAML_DECLARE(int)
yaml_parser_update_buffer(yaml_parser_t *parser, size_t length);

#include <stdlib.h>
#include <stdio.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

/*
 * Test cases are stolen from
 * http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt
 */

typedef struct {
    char *title;
    char *test;
    int result;
} test_case;

test_case utf8_sequences[] = {
    /* {"title", "test 1|test 2|...|test N!", (0 or 1)}, */

    {"a simple test", "'test' is '\xd0\xbf\xd1\x80\xd0\xbe\xd0\xb2\xd0\xb5\xd1\x80\xd0\xba\xd0\xb0' in Russian!", 1},
    {"an empty line", "!", 1},

    {"u-0 is a control character", "\x00!", 0},
    {"u-80 is a control character", "\xc2\x80!", 0},
    {"u-800 is valid", "\xe0\xa0\x80!", 1},
    {"u-10000 is valid", "\xf0\x90\x80\x80!", 1},
    {"5 bytes sequences are not allowed", "\xf8\x88\x80\x80\x80!", 0},
    {"6 bytes sequences are not allowed", "\xfc\x84\x80\x80\x80\x80!", 0},

    {"u-7f is a control character", "\x7f!", 0},
    {"u-7FF is valid", "\xdf\xbf!", 1},
    {"u-FFFF is a control character", "\xef\xbf\xbf!", 0},
    {"u-1FFFFF is too large", "\xf7\xbf\xbf\xbf!", 0},
    {"u-3FFFFFF is 5 bytes", "\xfb\xbf\xbf\xbf\xbf!", 0},
    {"u-7FFFFFFF is 6 bytes", "\xfd\xbf\xbf\xbf\xbf\xbf!", 0},

    {"u-D7FF", "\xed\x9f\xbf!", 1},
    {"u-E000", "\xee\x80\x80!", 1},
    {"u-FFFD", "\xef\xbf\xbd!", 1},
    {"u-10FFFF", "\xf4\x8f\xbf\xbf!", 1},
    {"u-110000", "\xf4\x90\x80\x80!", 0},

    {"first continuation byte", "\x80!", 0},
    {"last continuation byte", "\xbf!", 0},

    {"2 continuation bytes", "\x80\xbf!", 0},
    {"3 continuation bytes", "\x80\xbf\x80!", 0},
    {"4 continuation bytes", "\x80\xbf\x80\xbf!", 0},
    {"5 continuation bytes", "\x80\xbf\x80\xbf\x80!", 0},
    {"6 continuation bytes", "\x80\xbf\x80\xbf\x80\xbf!", 0},
    {"7 continuation bytes", "\x80\xbf\x80\xbf\x80\xbf\x80!", 0},

    {"sequence of all 64 possible continuation bytes",
     "\x80|\x81|\x82|\x83|\x84|\x85|\x86|\x87|\x88|\x89|\x8a|\x8b|\x8c|\x8d|\x8e|\x8f|"
     "\x90|\x91|\x92|\x93|\x94|\x95|\x96|\x97|\x98|\x99|\x9a|\x9b|\x9c|\x9d|\x9e|\x9f|"
     "\xa0|\xa1|\xa2|\xa3|\xa4|\xa5|\xa6|\xa7|\xa8|\xa9|\xaa|\xab|\xac|\xad|\xae|\xaf|"
     "\xb0|\xb1|\xb2|\xb3|\xb4|\xb5|\xb6|\xb7|\xb8|\xb9|\xba|\xbb|\xbc|\xbd|\xbe|\xbf!", 0},
    {"32 first bytes of 2-byte sequences {0xc0-0xdf}",
     "\xc0 |\xc1 |\xc2 |\xc3 |\xc4 |\xc5 |\xc6 |\xc7 |\xc8 |\xc9 |\xca |\xcb |\xcc |\xcd |\xce |\xcf |"
     "\xd0 |\xd1 |\xd2 |\xd3 |\xd4 |\xd5 |\xd6 |\xd7 |\xd8 |\xd9 |\xda |\xdb |\xdc |\xdd |\xde |\xdf !", 0},
    {"16 first bytes of 3-byte sequences {0xe0-0xef}",
     "\xe0 |\xe1 |\xe2 |\xe3 |\xe4 |\xe5 |\xe6 |\xe7 |\xe8 |\xe9 |\xea |\xeb |\xec |\xed |\xee |\xef !", 0},
    {"8 first bytes of 4-byte sequences {0xf0-0xf7}", "\xf0 |\xf1 |\xf2 |\xf3 |\xf4 |\xf5 |\xf6 |\xf7 !", 0},
    {"4 first bytes of 5-byte sequences {0xf8-0xfb}", "\xf8 |\xf9 |\xfa |\xfb !", 0},
    {"2 first bytes of 6-byte sequences {0xfc-0xfd}", "\xfc |\xfd !", 0},

    {"sequences with last byte missing {u-0}",
     "\xc0|\xe0\x80|\xf0\x80\x80|\xf8\x80\x80\x80|\xfc\x80\x80\x80\x80!", 0},
    {"sequences with last byte missing {u-...FF}",
     "\xdf|\xef\xbf|\xf7\xbf\xbf|\xfb\xbf\xbf\xbf|\xfd\xbf\xbf\xbf\xbf!", 0},

    {"impossible bytes", "\xfe|\xff|\xfe\xfe\xff\xff!", 0},

    {"overlong sequences {u-2f}",
     "\xc0\xaf|\xe0\x80\xaf|\xf0\x80\x80\xaf|\xf8\x80\x80\x80\xaf|\xfc\x80\x80\x80\x80\xaf!", 0},

    {"maximum overlong sequences",
     "\xc1\xbf|\xe0\x9f\xbf|\xf0\x8f\xbf\xbf|\xf8\x87\xbf\xbf\xbf|\xfc\x83\xbf\xbf\xbf\xbf!", 0},

    {"overlong representation of the NUL character",
     "\xc0\x80|\xe0\x80\x80|\xf0\x80\x80\x80|\xf8\x80\x80\x80\x80|\xfc\x80\x80\x80\x80\x80!", 0},

    {"single UTF-16 surrogates",
     "\xed\xa0\x80|\xed\xad\xbf|\xed\xae\x80|\xed\xaf\xbf|\xed\xb0\x80|\xed\xbe\x80|\xed\xbf\xbf!", 0},

    {"paired UTF-16 surrogates",
     "\xed\xa0\x80\xed\xb0\x80|\xed\xa0\x80\xed\xbf\xbf|\xed\xad\xbf\xed\xb0\x80|"
     "\xed\xad\xbf\xed\xbf\xbf|\xed\xae\x80\xed\xb0\x80|\xed\xae\x80\xed\xbf\xbf|"
     "\xed\xaf\xbf\xed\xb0\x80|\xed\xaf\xbf\xed\xbf\xbf!", 0},

    {"other illegal code positions", "\xef\xbf\xbe|\xef\xbf\xbf!", 0},

    {NULL, NULL, 0}
};

test_case boms[] = {
    
    /* {"title", "test!", lenth}, */
    
    {"no bom (utf-8)", "Hi is \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82!", 13},
    {"bom (utf-8)", "\xef\xbb\xbfHi is \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82!", 13},
    {"bom (utf-16-le)", "\xff\xfeH\x00i\x00 \x00i\x00s\x00 \x00\x1f\x04@\x04""8\x04""2\x04""5\x04""B\x04!", 13},
    {"bom (utf-16-be)", "\xfe\xff\x00H\x00i\x00 \x00i\x00s\x00 \x04\x1f\x04@\x04""8\x04""2\x04""5\x04""B!", 13},
    {NULL, NULL, 0}
};

char *bom_original = "Hi is \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82";

int check_utf8_sequences(void)
{
    yaml_parser_t parser;
    int failed = 0;
    int k;
    printf("checking utf-8 sequences...\n");
    for (k = 0; utf8_sequences[k].test; k++) {
        char *title = utf8_sequences[k].title;
        int check = utf8_sequences[k].result;
        int result;
        char *start = utf8_sequences[k].test;
        char *end = start;
        printf("\t%s:\n", title);
        while(1) {
            while (*end != '|' && *end != '!') end++;
            yaml_parser_initialize(&parser);
            yaml_parser_set_input_string(&parser, (unsigned char *)start, end-start);
            result = yaml_parser_update_buffer(&parser, end-start);
            if (result != check) {
                printf("\t\t- ");
                failed ++;
            }
            else {
                printf("\t\t+ ");
            }
            if (!parser.error) {
                printf("(no error)\n");
            }
            else if (parser.error == YAML_READER_ERROR) {
                if (parser.problem_value != -1) {
                    printf("(reader error: %s: #%X at %ld)\n",
                            parser.problem, parser.problem_value, (long)parser.problem_offset);
                }
                else {
                    printf("(reader error: %s at %ld)\n",
                            parser.problem, (long)parser.problem_offset);
                }
            }
            if (*end == '!') break;
            start = ++end;
            yaml_parser_delete(&parser);
        };
        printf("\n");
    }
    printf("checking utf-8 sequences: %d fail(s)\n", failed);
    return failed;
}

int check_boms(void)
{
    yaml_parser_t parser;
    int failed = 0;
    int k;
    printf("checking boms...\n");
    for (k = 0; boms[k].test; k++) {
        char *title = boms[k].title;
        int check = boms[k].result;
        int result;
        char *start = boms[k].test;
        char *end = start;
        while (*end != '!') end++;
        printf("\t%s: ", title);
        yaml_parser_initialize(&parser);
        yaml_parser_set_input_string(&parser, (unsigned char *)start, end-start);
        result = yaml_parser_update_buffer(&parser, end-start);
        if (!result) {
            printf("- (reader error: %s at %ld)\n", parser.problem, (long)parser.problem_offset);
            failed++;
        }
        else {
            if (parser.unread != check) {
                printf("- (length=%ld while expected length=%d)\n", (long)parser.unread, check);
                failed++;
            }
            else if (memcmp(parser.buffer.start, bom_original, check) != 0) {
                printf("- (value '%s' does not equal to the original value '%s')\n", parser.buffer.start, bom_original);
                failed++;
            }
            else {
                printf("+\n");
            }
        }
        yaml_parser_delete(&parser);
    }
    printf("checking boms: %d fail(s)\n", failed);
    return failed;
}

#define LONG    100000

int check_long_utf8(void)
{
    yaml_parser_t parser;
    int k = 0;
    int j;
    int failed = 0;
    unsigned char ch0, ch1;
    unsigned char *buffer = (unsigned char *)malloc(3+LONG*2);
    assert(buffer);
    printf("checking a long utf8 sequence...\n");
    buffer[k++] = '\xef';
    buffer[k++] = '\xbb';
    buffer[k++] = '\xbf';
    for (j = 0; j < LONG; j ++) {
        if (j % 2) {
            buffer[k++] = '\xd0';
            buffer[k++] = '\x90';
        }
        else {
            buffer[k++] = '\xd0';
            buffer[k++] = '\xaf';
        }
    }
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, buffer, 3+LONG*2);
    for (k = 0; k < LONG; k++) {
        if (!parser.unread) {
            if (!yaml_parser_update_buffer(&parser, 1)) {
                printf("\treader error: %s at %ld\n", parser.problem, (long)parser.problem_offset);
                failed = 1;
                break;
            }
        }
        if (!parser.unread) {
            printf("\tnot enough characters at %d\n", k);
            failed = 1;
            break;
        }
        if (k % 2) {
            ch0 = '\xd0';
            ch1 = '\x90';
        }
        else {
            ch0 = '\xd0';
            ch1 = '\xaf';
        }
        if (parser.buffer.pointer[0] != ch0 || parser.buffer.pointer[1] != ch1) {
            printf("\tincorrect UTF-8 sequence: %X %X instead of %X %X\n",
                    (int)parser.buffer.pointer[0], (int)parser.buffer.pointer[1],
                    (int)ch0, (int)ch1);
            failed = 1;
            break;
        }
        parser.buffer.pointer += 2;
        parser.unread -= 1;
    }
    if (!failed) {
        if (!yaml_parser_update_buffer(&parser, 1)) {
            printf("\treader error: %s at %ld\n", parser.problem, (long)parser.problem_offset);
            failed = 1;
        }
        else if (parser.buffer.pointer[0] != '\0') {
            printf("\texpected NUL, found %X (eof=%d, unread=%ld)\n", (int)parser.buffer.pointer[0], parser.eof, (long)parser.unread);
            failed = 1;
        }
    }
    yaml_parser_delete(&parser);
    free(buffer);
    printf("checking a long utf8 sequence: %d fail(s)\n", failed);
    return failed;
}

int check_long_utf16(void)
{
    yaml_parser_t parser;
    int k = 0;
    int j;
    int failed = 0;
    unsigned char ch0, ch1;
    unsigned char *buffer = (unsigned char *)malloc(2+LONG*2);
    assert(buffer);
    printf("checking a long utf16 sequence...\n");
    buffer[k++] = '\xff';
    buffer[k++] = '\xfe';
    for (j = 0; j < LONG; j ++) {
        if (j % 2) {
            buffer[k++] = '\x10';
            buffer[k++] = '\x04';
        }
        else {
            buffer[k++] = '/';
            buffer[k++] = '\x04';
        }
    }
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, buffer, 2+LONG*2);
    for (k = 0; k < LONG; k++) {
        if (!parser.unread) {
            if (!yaml_parser_update_buffer(&parser, 1)) {
                printf("\treader error: %s at %ld\n", parser.problem, (long)parser.problem_offset);
                failed = 1;
                break;
            }
        }
        if (!parser.unread) {
            printf("\tnot enough characters at %d\n", k);
            failed = 1;
            break;
        }
        if (k % 2) {
            ch0 = '\xd0';
            ch1 = '\x90';
        }
        else {
            ch0 = '\xd0';
            ch1 = '\xaf';
        }
        if (parser.buffer.pointer[0] != ch0 || parser.buffer.pointer[1] != ch1) {
            printf("\tincorrect UTF-8 sequence: %X %X instead of %X %X\n",
                    (int)parser.buffer.pointer[0], (int)parser.buffer.pointer[1],
                    (int)ch0, (int)ch1);
            failed = 1;
            break;
        }
        parser.buffer.pointer += 2;
        parser.unread -= 1;
    }
    if (!failed) {
        if (!yaml_parser_update_buffer(&parser, 1)) {
            printf("\treader error: %s at %ld\n", parser.problem, (long)parser.problem_offset);
            failed = 1;
        }
        else if (parser.buffer.pointer[0] != '\0') {
            printf("\texpected NUL, found %X (eof=%d, unread=%ld)\n", (int)parser.buffer.pointer[0], parser.eof, (long)parser.unread);
            failed = 1;
        }
    }
    yaml_parser_delete(&parser);
    free(buffer);
    printf("checking a long utf16 sequence: %d fail(s)\n", failed);
    return failed;
}

int
main(void)
{
    return check_utf8_sequences() + check_boms() + check_long_utf8() + check_long_utf16();
}
