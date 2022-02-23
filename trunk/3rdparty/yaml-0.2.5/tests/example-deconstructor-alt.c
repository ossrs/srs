
#include <yaml.h>

#include <stdlib.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
    int help = 0;
    int canonical = 0;
    int unicode = 0;
    int k;
    int done = 0;

    yaml_parser_t parser;
    yaml_emitter_t emitter;
    yaml_event_t input_event;
    yaml_document_t output_document;

    int root;

    /* Clear the objects. */

    memset(&parser, 0, sizeof(parser));
    memset(&emitter, 0, sizeof(emitter));
    memset(&input_event, 0, sizeof(input_event));
    memset(&output_document, 0, sizeof(output_document));

    /* Analyze command line options. */

    for (k = 1; k < argc; k ++)
    {
        if (strcmp(argv[k], "-h") == 0
                || strcmp(argv[k], "--help") == 0) {
            help = 1;
        }

        else if (strcmp(argv[k], "-c") == 0
                || strcmp(argv[k], "--canonical") == 0) {
            canonical = 1;
        }

        else if (strcmp(argv[k], "-u") == 0
                || strcmp(argv[k], "--unicode") == 0) {
            unicode = 1;
        }

        else {
            fprintf(stderr, "Unrecognized option: %s\n"
                    "Try `%s --help` for more information.\n",
                    argv[k], argv[0]);
            return 1;
        }
    }

    /* Display the help string. */

    if (help)
    {
        printf("%s <input\n"
                "or\n%s -h | --help\nDeconstruct a YAML stream\n\nOptions:\n"
                "-h, --help\t\tdisplay this help and exit\n"
                "-c, --canonical\t\toutput in the canonical YAML format\n"
                "-u, --unicode\t\toutput unescaped non-ASCII characters\n",
                argv[0], argv[0]);
        return 0;
    }

    /* Initialize the parser and emitter objects. */

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Could not initialize the parser object\n");
        return 1;
    }

    if (!yaml_emitter_initialize(&emitter)) {
        yaml_parser_delete(&parser);
        fprintf(stderr, "Could not inialize the emitter object\n");
        return 1;
    }

    /* Set the parser parameters. */

    yaml_parser_set_input_file(&parser, stdin);

    /* Set the emitter parameters. */

    yaml_emitter_set_output_file(&emitter, stdout);

    yaml_emitter_set_canonical(&emitter, canonical);
    yaml_emitter_set_unicode(&emitter, unicode);

    /* Create and emit the STREAM-START event. */

    if (!yaml_emitter_open(&emitter))
        goto emitter_error;

    /* Create a output_document object. */

    if (!yaml_document_initialize(&output_document, NULL, NULL, NULL, 0, 0))
        goto document_error;

    /* Create the root sequence. */

    root = yaml_document_add_sequence(&output_document, NULL,
            YAML_BLOCK_SEQUENCE_STYLE);
    if (!root) goto document_error;

    /* Loop through the input events. */

    while (!done)
    {
        int properties, key, value, map, seq;

        /* Get the next event. */

        if (!yaml_parser_parse(&parser, &input_event))
            goto parser_error;

        /* Check if this is the stream end. */

        if (input_event.type == YAML_STREAM_END_EVENT) {
            done = 1;
        }

        /* Create a mapping node and attach it to the root sequence. */

        properties = yaml_document_add_mapping(&output_document, NULL,
                YAML_BLOCK_MAPPING_STYLE);
        if (!properties) goto document_error;
        if (!yaml_document_append_sequence_item(&output_document,
                    root, properties)) goto document_error;

        /* Analyze the event. */

        switch (input_event.type)
        {
            case YAML_STREAM_START_EVENT:

                /* Add 'type': 'STREAM-START'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"STREAM-START", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Add 'encoding': <encoding>. */

                if (input_event.data.stream_start.encoding)
                {
                    yaml_encoding_t encoding
                        = input_event.data.stream_start.encoding;

                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"encoding", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            (yaml_char_t *)(encoding == YAML_UTF8_ENCODING ? "utf-8" :
                             encoding == YAML_UTF16LE_ENCODING ? "utf-16-le" :
                             encoding == YAML_UTF16BE_ENCODING ? "utf-16-be" :
                             "unknown"), -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }
                    
                break;

            case YAML_STREAM_END_EVENT:

                /* Add 'type': 'STREAM-END'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"STREAM-END", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                break;

            case YAML_DOCUMENT_START_EVENT:

                /* Add 'type': 'DOCUMENT-START'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"DOCUMENT-START", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Display the output_document version numbers. */

                if (input_event.data.document_start.version_directive)
                {
                    yaml_version_directive_t *version
                        = input_event.data.document_start.version_directive;
                    char number[64];

                    /* Add 'version': {}. */
                    
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"version", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    map = yaml_document_add_mapping(&output_document, NULL,
                            YAML_FLOW_MAPPING_STYLE);
                    if (!map) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, map)) goto document_error;

                    /* Add 'major': <number>. */

                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"major", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    sprintf(number, "%d", version->major);
                    value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_INT_TAG,
                        (yaml_char_t *)number, -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                map, key, value)) goto document_error;

                    /* Add 'minor': <number>. */

                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"minor", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    sprintf(number, "%d", version->minor);
                    value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_INT_TAG,
                        (yaml_char_t *)number, -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                map, key, value)) goto document_error;
                }

                /* Display the output_document tag directives. */

                if (input_event.data.document_start.tag_directives.start
                        != input_event.data.document_start.tag_directives.end)
                {
                    yaml_tag_directive_t *tag;

                    /* Add 'tags': []. */
                    
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"tags", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    seq = yaml_document_add_sequence(&output_document, NULL,
                            YAML_BLOCK_SEQUENCE_STYLE);
                    if (!seq) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, seq)) goto document_error;

                    for (tag = input_event.data.document_start.tag_directives.start;
                            tag != input_event.data.document_start.tag_directives.end;
                            tag ++)
                    {
                        /* Add {}. */

                        map = yaml_document_add_mapping(&output_document, NULL,
                                YAML_FLOW_MAPPING_STYLE);
                        if (!map) goto document_error;
                        if (!yaml_document_append_sequence_item(&output_document,
                                    seq, map)) goto document_error;

                        /* Add 'handle': <handle>. */

                        key = yaml_document_add_scalar(&output_document, NULL,
                            (yaml_char_t *)"handle", -1, YAML_PLAIN_SCALAR_STYLE);
                        if (!key) goto document_error;
                        value = yaml_document_add_scalar(&output_document, NULL,
                            tag->handle, -1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                        if (!value) goto document_error;
                        if (!yaml_document_append_mapping_pair(&output_document,
                                    map, key, value)) goto document_error;

                        /* Add 'prefix': <prefix>. */

                        key = yaml_document_add_scalar(&output_document, NULL,
                            (yaml_char_t *)"prefix", -1, YAML_PLAIN_SCALAR_STYLE);
                        if (!key) goto document_error;
                        value = yaml_document_add_scalar(&output_document, NULL,
                            tag->prefix, -1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                        if (!value) goto document_error;
                        if (!yaml_document_append_mapping_pair(&output_document,
                                    map, key, value)) goto document_error;
                    }
                }

                /* Add 'implicit': <flag>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"implicit", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_BOOL_TAG,
                        (yaml_char_t *)(input_event.data.document_start.implicit ?
                         "true" : "false"), -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                break;

            case YAML_DOCUMENT_END_EVENT:

                /* Add 'type': 'DOCUMENT-END'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"DOCUMENT-END", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Add 'implicit': <flag>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"implicit", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_BOOL_TAG,
                        (yaml_char_t *)(input_event.data.document_end.implicit ?
                         "true" : "false"), -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                break;

            case YAML_ALIAS_EVENT:

                /* Add 'type': 'ALIAS'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"ALIAS", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Add 'anchor': <anchor>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"anchor", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                        input_event.data.alias.anchor, -1,
                        YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                break;

            case YAML_SCALAR_EVENT:

                /* Add 'type': 'SCALAR'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"SCALAR", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Add 'anchor': <anchor>. */

                if (input_event.data.scalar.anchor)
                {
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"anchor", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            input_event.data.scalar.anchor, -1,
                            YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                /* Add 'tag': <tag>. */

                if (input_event.data.scalar.tag)
                {
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"tag", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            input_event.data.scalar.tag, -1,
                            YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                /* Add 'value': <value>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"value", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                        input_event.data.scalar.value,
                        input_event.data.scalar.length,
                        YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Display if the scalar tag is implicit. */

                /* Add 'implicit': {} */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"version", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                map = yaml_document_add_mapping(&output_document, NULL,
                        YAML_FLOW_MAPPING_STYLE);
                if (!map) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, map)) goto document_error;

                /* Add 'plain': <flag>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"plain", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_BOOL_TAG,
                        (yaml_char_t *)(input_event.data.scalar.plain_implicit ?
                         "true" : "false"), -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            map, key, value)) goto document_error;

                /* Add 'quoted': <flag>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"quoted", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_BOOL_TAG,
                        (yaml_char_t *)(input_event.data.scalar.quoted_implicit ?
                         "true" : "false"), -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            map, key, value)) goto document_error;

                /* Display the style information. */

                if (input_event.data.scalar.style)
                {
                    yaml_scalar_style_t style = input_event.data.scalar.style;

                    /* Add 'style': <style>. */

                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"style", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            (yaml_char_t *)(style == YAML_PLAIN_SCALAR_STYLE ? "plain" :
                             style == YAML_SINGLE_QUOTED_SCALAR_STYLE ?
                                    "single-quoted" :
                             style == YAML_DOUBLE_QUOTED_SCALAR_STYLE ?
                                    "double-quoted" :
                             style == YAML_LITERAL_SCALAR_STYLE ? "literal" :
                             style == YAML_FOLDED_SCALAR_STYLE ? "folded" :
                             "unknown"), -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                break;

            case YAML_SEQUENCE_START_EVENT:

                /* Add 'type': 'SEQUENCE-START'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"SEQUENCE-START", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Add 'anchor': <anchor>. */

                if (input_event.data.sequence_start.anchor)
                {
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"anchor", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            input_event.data.sequence_start.anchor, -1,
                            YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                /* Add 'tag': <tag>. */

                if (input_event.data.sequence_start.tag)
                {
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"tag", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            input_event.data.sequence_start.tag, -1,
                            YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                /* Add 'implicit': <flag>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"implicit", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_BOOL_TAG,
                        (yaml_char_t *)(input_event.data.sequence_start.implicit ?
                         "true" : "false"), -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Display the style information. */

                if (input_event.data.sequence_start.style)
                {
                    yaml_sequence_style_t style
                        = input_event.data.sequence_start.style;

                    /* Add 'style': <style>. */

                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"style", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            (yaml_char_t *)(style == YAML_BLOCK_SEQUENCE_STYLE ? "block" :
                             style == YAML_FLOW_SEQUENCE_STYLE ? "flow" :
                             "unknown"), -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                break;

            case YAML_SEQUENCE_END_EVENT:

                /* Add 'type': 'SEQUENCE-END'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"SEQUENCE-END", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                break;

            case YAML_MAPPING_START_EVENT:

                /* Add 'type': 'MAPPING-START'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"MAPPING-START", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Add 'anchor': <anchor>. */

                if (input_event.data.mapping_start.anchor)
                {
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"anchor", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            input_event.data.mapping_start.anchor, -1,
                            YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                /* Add 'tag': <tag>. */

                if (input_event.data.mapping_start.tag)
                {
                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"tag", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            input_event.data.mapping_start.tag, -1,
                            YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                /* Add 'implicit': <flag>. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"implicit", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, (yaml_char_t *)YAML_BOOL_TAG,
                        (yaml_char_t *)(input_event.data.mapping_start.implicit ?
                         "true" : "false"), -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                /* Display the style information. */

                if (input_event.data.mapping_start.style)
                {
                    yaml_mapping_style_t style
                        = input_event.data.mapping_start.style;

                    /* Add 'style': <style>. */

                    key = yaml_document_add_scalar(&output_document, NULL,
                        (yaml_char_t *)"style", -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!key) goto document_error;
                    value = yaml_document_add_scalar(&output_document, NULL,
                            (yaml_char_t *)(style == YAML_BLOCK_MAPPING_STYLE ? "block" :
                             style == YAML_FLOW_MAPPING_STYLE ? "flow" :
                             "unknown"), -1, YAML_PLAIN_SCALAR_STYLE);
                    if (!value) goto document_error;
                    if (!yaml_document_append_mapping_pair(&output_document,
                                properties, key, value)) goto document_error;
                }

                break;

            case YAML_MAPPING_END_EVENT:

                /* Add 'type': 'MAPPING-END'. */

                key = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"type", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!key) goto document_error;
                value = yaml_document_add_scalar(&output_document, NULL,
                    (yaml_char_t *)"MAPPING-END", -1, YAML_PLAIN_SCALAR_STYLE);
                if (!value) goto document_error;
                if (!yaml_document_append_mapping_pair(&output_document,
                            properties, key, value)) goto document_error;

                break;

            default:
                /* It couldn't really happen. */
                break;
        }

        /* Delete the event object. */

        yaml_event_delete(&input_event);
    }

    if (!yaml_emitter_dump(&emitter, &output_document))
        goto emitter_error;
    if (!yaml_emitter_close(&emitter))
        goto emitter_error;

    yaml_parser_delete(&parser);
    yaml_emitter_delete(&emitter);

    return 0;

parser_error:

    /* Display a parser error message. */

    switch (parser.error)
    {
        case YAML_MEMORY_ERROR:
            fprintf(stderr, "Memory error: Not enough memory for parsing\n");
            break;

        case YAML_READER_ERROR:
            if (parser.problem_value != -1) {
                fprintf(stderr, "Reader error: %s: #%X at %zd\n", parser.problem,
                        parser.problem_value, parser.problem_offset);
            }
            else {
                fprintf(stderr, "Reader error: %s at %zd\n", parser.problem,
                        parser.problem_offset);
            }
            break;

        case YAML_SCANNER_ERROR:
            if (parser.context) {
                fprintf(stderr, "Scanner error: %s at line %lu, column %lu\n"
                        "%s at line %lu, column %lu\n", parser.context,
                        parser.context_mark.line+1, parser.context_mark.column+1,
                        parser.problem, parser.problem_mark.line+1,
                        parser.problem_mark.column+1);
            }
            else {
                fprintf(stderr, "Scanner error: %s at line %lu, column %lu\n",
                        parser.problem, parser.problem_mark.line+1,
                        parser.problem_mark.column+1);
            }
            break;

        case YAML_PARSER_ERROR:
            if (parser.context) {
                fprintf(stderr, "Parser error: %s at line %lu, column %lu\n"
                        "%s at line %lu, column %lu\n", parser.context,
                        parser.context_mark.line+1, parser.context_mark.column+1,
                        parser.problem, parser.problem_mark.line+1,
                        parser.problem_mark.column+1);
            }
            else {
                fprintf(stderr, "Parser error: %s at line %lu, column %lu\n",
                        parser.problem, parser.problem_mark.line+1,
                        parser.problem_mark.column+1);
            }
            break;

        default:
            /* Couldn't happen. */
            fprintf(stderr, "Internal error\n");
            break;
    }

    yaml_event_delete(&input_event);
    yaml_document_delete(&output_document);
    yaml_parser_delete(&parser);
    yaml_emitter_delete(&emitter);

    return 1;

emitter_error:

    /* Display an emitter error message. */

    switch (emitter.error)
    {
        case YAML_MEMORY_ERROR:
            fprintf(stderr, "Memory error: Not enough memory for emitting\n");
            break;

        case YAML_WRITER_ERROR:
            fprintf(stderr, "Writer error: %s\n", emitter.problem);
            break;

        case YAML_EMITTER_ERROR:
            fprintf(stderr, "Emitter error: %s\n", emitter.problem);
            break;

        default:
            /* Couldn't happen. */
            fprintf(stderr, "Internal error\n");
            break;
    }

    yaml_event_delete(&input_event);
    yaml_document_delete(&output_document);
    yaml_parser_delete(&parser);
    yaml_emitter_delete(&emitter);

    return 1;

document_error:

    fprintf(stderr, "Memory error: Not enough memory for creating a document\n");

    yaml_event_delete(&input_event);
    yaml_document_delete(&output_document);
    yaml_parser_delete(&parser);
    yaml_emitter_delete(&emitter);

    return 1;
}

