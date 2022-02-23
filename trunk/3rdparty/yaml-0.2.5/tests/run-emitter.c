#include <yaml.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

#define BUFFER_SIZE 65536
#define MAX_EVENTS  1024

int copy_event(yaml_event_t *event_to, yaml_event_t *event_from)
{
    switch (event_from->type)
    {
        case YAML_STREAM_START_EVENT:
            return yaml_stream_start_event_initialize(event_to,
                    event_from->data.stream_start.encoding);

        case YAML_STREAM_END_EVENT:
            return yaml_stream_end_event_initialize(event_to);

        case YAML_DOCUMENT_START_EVENT:
            return yaml_document_start_event_initialize(event_to,
                    event_from->data.document_start.version_directive,
                    event_from->data.document_start.tag_directives.start,
                    event_from->data.document_start.tag_directives.end,
                    event_from->data.document_start.implicit);

        case YAML_DOCUMENT_END_EVENT:
            return yaml_document_end_event_initialize(event_to,
                    event_from->data.document_end.implicit);

        case YAML_ALIAS_EVENT:
            return yaml_alias_event_initialize(event_to,
                    event_from->data.alias.anchor);

        case YAML_SCALAR_EVENT:
            return yaml_scalar_event_initialize(event_to,
                    event_from->data.scalar.anchor,
                    event_from->data.scalar.tag,
                    event_from->data.scalar.value,
                    event_from->data.scalar.length,
                    event_from->data.scalar.plain_implicit,
                    event_from->data.scalar.quoted_implicit,
                    event_from->data.scalar.style);

        case YAML_SEQUENCE_START_EVENT:
            return yaml_sequence_start_event_initialize(event_to,
                    event_from->data.sequence_start.anchor,
                    event_from->data.sequence_start.tag,
                    event_from->data.sequence_start.implicit,
                    event_from->data.sequence_start.style);

        case YAML_SEQUENCE_END_EVENT:
            return yaml_sequence_end_event_initialize(event_to);

        case YAML_MAPPING_START_EVENT:
            return yaml_mapping_start_event_initialize(event_to,
                    event_from->data.mapping_start.anchor,
                    event_from->data.mapping_start.tag,
                    event_from->data.mapping_start.implicit,
                    event_from->data.mapping_start.style);

        case YAML_MAPPING_END_EVENT:
            return yaml_mapping_end_event_initialize(event_to);

        default:
            assert(1);
    }

    return 0;
}

int compare_events(yaml_event_t *event1, yaml_event_t *event2)
{
    int k;

    if (event1->type != event2->type)
        return 0;

    switch (event1->type)
    {
        case YAML_STREAM_START_EVENT:
            return 1;
            /* return (event1->data.stream_start.encoding ==
                    event2->data.stream_start.encoding); */

        case YAML_DOCUMENT_START_EVENT:
            if ((event1->data.document_start.version_directive && !event2->data.document_start.version_directive)
                    || (!event1->data.document_start.version_directive && event2->data.document_start.version_directive)
                    || (event1->data.document_start.version_directive && event2->data.document_start.version_directive
                        && (event1->data.document_start.version_directive->major != event2->data.document_start.version_directive->major
                            || event1->data.document_start.version_directive->minor != event2->data.document_start.version_directive->minor)))
                return 0;
            if ((event1->data.document_start.tag_directives.end - event1->data.document_start.tag_directives.start) !=
                    (event2->data.document_start.tag_directives.end - event2->data.document_start.tag_directives.start))
                return 0;
            for (k = 0; k < (event1->data.document_start.tag_directives.end - event1->data.document_start.tag_directives.start); k ++) {
                if ((strcmp((char *)event1->data.document_start.tag_directives.start[k].handle,
                                (char *)event2->data.document_start.tag_directives.start[k].handle) != 0)
                        || (strcmp((char *)event1->data.document_start.tag_directives.start[k].prefix,
                            (char *)event2->data.document_start.tag_directives.start[k].prefix) != 0))
                    return 0;
            }
            /* if (event1->data.document_start.implicit != event2->data.document_start.implicit)
                return 0; */
            return 1;

        case YAML_DOCUMENT_END_EVENT:
            return 1;
            /* return (event1->data.document_end.implicit ==
                    event2->data.document_end.implicit); */

        case YAML_ALIAS_EVENT:
            return (strcmp((char *)event1->data.alias.anchor,
                        (char *)event2->data.alias.anchor) == 0);

        case YAML_SCALAR_EVENT:
            if ((event1->data.scalar.anchor && !event2->data.scalar.anchor)
                    || (!event1->data.scalar.anchor && event2->data.scalar.anchor)
                    || (event1->data.scalar.anchor && event2->data.scalar.anchor
                        && strcmp((char *)event1->data.scalar.anchor,
                            (char *)event2->data.scalar.anchor) != 0))
                return 0;
            if ((event1->data.scalar.tag && !event2->data.scalar.tag
                        && strcmp((char *)event1->data.scalar.tag, "!") != 0)
                    || (!event1->data.scalar.tag && event2->data.scalar.tag
                        && strcmp((char *)event2->data.scalar.tag, "!") != 0)
                    || (event1->data.scalar.tag && event2->data.scalar.tag
                        && strcmp((char *)event1->data.scalar.tag,
                            (char *)event2->data.scalar.tag) != 0))
                return 0;
            if ((event1->data.scalar.length != event2->data.scalar.length)
                    || memcmp(event1->data.scalar.value, event2->data.scalar.value,
                        event1->data.scalar.length) != 0)
                return 0;
            if ((event1->data.scalar.plain_implicit != event2->data.scalar.plain_implicit)
                    || (event1->data.scalar.quoted_implicit != event2->data.scalar.quoted_implicit)
                    /* || (event2->data.scalar.style != event2->data.scalar.style) */)
                return 0;
            return 1;

        case YAML_SEQUENCE_START_EVENT:
            if ((event1->data.sequence_start.anchor && !event2->data.sequence_start.anchor)
                    || (!event1->data.sequence_start.anchor && event2->data.sequence_start.anchor)
                    || (event1->data.sequence_start.anchor && event2->data.sequence_start.anchor
                        && strcmp((char *)event1->data.sequence_start.anchor,
                            (char *)event2->data.sequence_start.anchor) != 0))
                return 0;
            if ((event1->data.sequence_start.tag && !event2->data.sequence_start.tag)
                    || (!event1->data.sequence_start.tag && event2->data.sequence_start.tag)
                    || (event1->data.sequence_start.tag && event2->data.sequence_start.tag
                        && strcmp((char *)event1->data.sequence_start.tag,
                            (char *)event2->data.sequence_start.tag) != 0))
                return 0;
            if ((event1->data.sequence_start.implicit != event2->data.sequence_start.implicit)
                    /* || (event2->data.sequence_start.style != event2->data.sequence_start.style) */)
                return 0;
            return 1;

        case YAML_MAPPING_START_EVENT:
            if ((event1->data.mapping_start.anchor && !event2->data.mapping_start.anchor)
                    || (!event1->data.mapping_start.anchor && event2->data.mapping_start.anchor)
                    || (event1->data.mapping_start.anchor && event2->data.mapping_start.anchor
                        && strcmp((char *)event1->data.mapping_start.anchor,
                            (char *)event2->data.mapping_start.anchor) != 0))
                return 0;
            if ((event1->data.mapping_start.tag && !event2->data.mapping_start.tag)
                    || (!event1->data.mapping_start.tag && event2->data.mapping_start.tag)
                    || (event1->data.mapping_start.tag && event2->data.mapping_start.tag
                        && strcmp((char *)event1->data.mapping_start.tag,
                            (char *)event2->data.mapping_start.tag) != 0))
                return 0;
            if ((event1->data.mapping_start.implicit != event2->data.mapping_start.implicit)
                    /* || (event2->data.mapping_start.style != event2->data.mapping_start.style) */)
                return 0;
            return 1;

        default:
            return 1;
    }
}

int print_output(char *name, unsigned char *buffer, size_t size, int count)
{
    FILE *file;
    char data[BUFFER_SIZE];
    size_t data_size = 1;
    size_t total_size = 0;
    if (count >= 0) {
        printf("FAILED (at the event #%d)\nSOURCE:\n", count+1);
    }
    file = fopen(name, "rb");
    assert(file);
    while (data_size > 0) {
        data_size = fread(data, 1, BUFFER_SIZE, file);
        assert(!ferror(file));
        if (!data_size) break;
        assert(fwrite(data, 1, data_size, stdout) == data_size);
        total_size += data_size;
        if (feof(file)) break;
    }
    fclose(file);
    printf("#### (length: %ld)\n", (long)total_size);
    printf("OUTPUT:\n%s#### (length: %ld)\n", buffer, (long)size);
    return 0;
}

int
main(int argc, char *argv[])
{
    int number;
    int canonical = 0;
    int unicode = 0;

    number = 1;
    while (number < argc) {
        if (strcmp(argv[number], "-c") == 0) {
            canonical = 1;
        }
        else if (strcmp(argv[number], "-u") == 0) {
            unicode = 1;
        }
        else if (argv[number][0] == '-') {
            printf("Unknown option: '%s'\n", argv[number]);
            return 0;
        }
        if (argv[number][0] == '-') {
            if (number < argc-1) {
                memmove(argv+number, argv+number+1, (argc-number-1)*sizeof(char *));
            }
            argc --;
        }
        else {
            number ++;
        }
    }

    if (argc < 2) {
        printf("Usage: %s [-c] [-u] file1.yaml ...\n", argv[0]);
        return 0;
    }

    for (number = 1; number < argc; number ++)
    {
        FILE *file;
        yaml_parser_t parser;
        yaml_emitter_t emitter;
        yaml_event_t event;
        unsigned char buffer[BUFFER_SIZE+1];
        size_t written = 0;
        yaml_event_t events[MAX_EVENTS];
        size_t event_number = 0;
        int done = 0;
        int count = 0;
        int error = 0;
        int k;
        memset(buffer, 0, BUFFER_SIZE+1);
        memset(events, 0, MAX_EVENTS*sizeof(yaml_event_t));

        printf("[%d] Parsing, emitting, and parsing again '%s': ", number, argv[number]);
        fflush(stdout);

        file = fopen(argv[number], "rb");
        assert(file);

        assert(yaml_parser_initialize(&parser));
        yaml_parser_set_input_file(&parser, file);
        assert(yaml_emitter_initialize(&emitter));
        if (canonical) {
            yaml_emitter_set_canonical(&emitter, 1);
        }
        if (unicode) {
            yaml_emitter_set_unicode(&emitter, 1);
        }
        yaml_emitter_set_output_string(&emitter, buffer, BUFFER_SIZE, &written);

        while (!done)
        {
            if (!yaml_parser_parse(&parser, &event)) {
                error = 1;
                break;
            }

            done = (event.type == YAML_STREAM_END_EVENT);
            assert(event_number < MAX_EVENTS);
            assert(copy_event(&(events[event_number++]), &event));
            assert(yaml_emitter_emit(&emitter, &event) || 
                    print_output(argv[number], buffer, written, count));
            count ++;
        }

        yaml_parser_delete(&parser);
        assert(!fclose(file));
        yaml_emitter_delete(&emitter);

        if (!error)
        {
            count = done = 0;
            assert(yaml_parser_initialize(&parser));
            yaml_parser_set_input_string(&parser, buffer, written);

            while (!done)
            {
                assert(yaml_parser_parse(&parser, &event) || print_output(argv[number], buffer, written, count));
                done = (event.type == YAML_STREAM_END_EVENT);
                assert(compare_events(events+count, &event) || print_output(argv[number], buffer, written, count));
                yaml_event_delete(&event);
                count ++;
            }
            yaml_parser_delete(&parser);
        }

        for (k = 0; k < event_number; k ++) {
            yaml_event_delete(events+k);
        }

        printf("PASSED (length: %ld)\n", (long)written);
        print_output(argv[number], buffer, written, -1);
    }

    return 0;
}
