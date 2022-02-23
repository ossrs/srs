
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
    yaml_event_t output_event;

    /* Clear the objects. */

    memset(&parser, 0, sizeof(parser));
    memset(&emitter, 0, sizeof(emitter));
    memset(&input_event, 0, sizeof(input_event));
    memset(&output_event, 0, sizeof(output_event));

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

    if (!yaml_stream_start_event_initialize(&output_event, YAML_UTF8_ENCODING))
        goto event_error;
    if (!yaml_emitter_emit(&emitter, &output_event))
        goto emitter_error;

    /* Create and emit the DOCUMENT-START event. */

    if (!yaml_document_start_event_initialize(&output_event,
                NULL, NULL, NULL, 0))
        goto event_error;
    if (!yaml_emitter_emit(&emitter, &output_event))
        goto emitter_error;

    /* Create and emit the SEQUENCE-START event. */

    if (!yaml_sequence_start_event_initialize(&output_event,
                NULL, (yaml_char_t *)"tag:yaml.org,2002:seq", 1,
                YAML_BLOCK_SEQUENCE_STYLE))
        goto event_error;
    if (!yaml_emitter_emit(&emitter, &output_event))
        goto emitter_error;

    /* Loop through the input events. */

    while (!done)
    {
        /* Get the next event. */

        if (!yaml_parser_parse(&parser, &input_event))
            goto parser_error;

        /* Check if this is the stream end. */

        if (input_event.type == YAML_STREAM_END_EVENT) {
            done = 1;
        }

        /* Create and emit a MAPPING-START event. */

        if (!yaml_mapping_start_event_initialize(&output_event,
                    NULL, (yaml_char_t *)"tag:yaml.org,2002:map", 1,
                    YAML_BLOCK_MAPPING_STYLE))
            goto event_error;
        if (!yaml_emitter_emit(&emitter, &output_event))
            goto emitter_error;

        /* Analyze the event. */

        switch (input_event.type)
        {
            case YAML_STREAM_START_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'STREAM-START'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"STREAM-START", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display encoding information. */

                if (input_event.data.stream_start.encoding)
                {
                    yaml_encoding_t encoding
                        = input_event.data.stream_start.encoding;

                    /* Write 'encoding'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"encoding", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the stream encoding. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                (yaml_char_t *)(encoding == YAML_UTF8_ENCODING ? "utf-8" :
                                 encoding == YAML_UTF16LE_ENCODING ? "utf-16-le" :
                                 encoding == YAML_UTF16BE_ENCODING ? "utf-16-be" :
                                 "unknown"), -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                break;

            case YAML_STREAM_END_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'STREAM-END'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"STREAM-END", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                break;

            case YAML_DOCUMENT_START_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'DOCUMENT-START'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"DOCUMENT-START", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the document version numbers. */

                if (input_event.data.document_start.version_directive)
                {
                    yaml_version_directive_t *version
                        = input_event.data.document_start.version_directive;
                    char number[64];

                    /* Write 'version'. */
                    
                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"version", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write '{'. */

                    if (!yaml_mapping_start_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:map", 1,
                                YAML_FLOW_MAPPING_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write 'major'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"major", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write a number. */

                    sprintf(number, "%d", version->major);
                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:int", (yaml_char_t *)number, -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write 'minor'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"minor", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write a number. */

                    sprintf(number, "%d", version->minor);
                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:int", (yaml_char_t *)number, -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write '}'. */

                    if (!yaml_mapping_end_event_initialize(&output_event))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Display the document tag directives. */

                if (input_event.data.document_start.tag_directives.start
                        != input_event.data.document_start.tag_directives.end)
                {
                    yaml_tag_directive_t *tag;

                    /* Write 'tags'. */
                    
                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"tags", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Start a block sequence. */

                    if (!yaml_sequence_start_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:seq", 1,
                                YAML_BLOCK_SEQUENCE_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    for (tag = input_event.data.document_start.tag_directives.start;
                            tag != input_event.data.document_start.tag_directives.end;
                            tag ++)
                    {
                        /* Write '{'. */

                        if (!yaml_mapping_start_event_initialize(&output_event,
                                    NULL, (yaml_char_t *)"tag:yaml.org,2002:map", 1,
                                    YAML_FLOW_MAPPING_STYLE))
                            goto event_error;
                        if (!yaml_emitter_emit(&emitter, &output_event))
                            goto emitter_error;

                        /* Write 'handle'. */

                        if (!yaml_scalar_event_initialize(&output_event,
                                    NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"handle", -1,
                                    1, 1, YAML_PLAIN_SCALAR_STYLE))
                            goto event_error;
                        if (!yaml_emitter_emit(&emitter, &output_event))
                            goto emitter_error;

                        /* Write the tag directive handle. */

                        if (!yaml_scalar_event_initialize(&output_event,
                                    NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                    tag->handle, -1,
                                    0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                            goto event_error;
                        if (!yaml_emitter_emit(&emitter, &output_event))
                            goto emitter_error;

                        /* Write 'prefix'. */

                        if (!yaml_scalar_event_initialize(&output_event,
                                    NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"prefix", -1,
                                    1, 1, YAML_PLAIN_SCALAR_STYLE))
                            goto event_error;
                        if (!yaml_emitter_emit(&emitter, &output_event))
                            goto emitter_error;

                        /* Write the tag directive prefix. */

                        if (!yaml_scalar_event_initialize(&output_event,
                                    NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                    tag->prefix, -1,
                                    0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                            goto event_error;
                        if (!yaml_emitter_emit(&emitter, &output_event))
                            goto emitter_error;

                        /* Write '}'. */

                        if (!yaml_mapping_end_event_initialize(&output_event))
                            goto event_error;
                        if (!yaml_emitter_emit(&emitter, &output_event))
                            goto emitter_error;
                    }

                    /* End a block sequence. */

                    if (!yaml_sequence_end_event_initialize(&output_event))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Write 'implicit'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"implicit", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write if the document is implicit. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:bool",
                            (yaml_char_t *)(input_event.data.document_start.implicit ?
                             "true" : "false"), -1,
                            1, 0, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                break;

            case YAML_DOCUMENT_END_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'DOCUMENT-END'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"DOCUMENT-END", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'implicit'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"implicit", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write if the document is implicit. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:bool",
                            (yaml_char_t *)(input_event.data.document_end.implicit ?
                             "true" : "false"), -1,
                            1, 0, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                break;

            case YAML_ALIAS_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'ALIAS'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"ALIAS", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'anchor'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"anchor", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write the alias anchor. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                            input_event.data.alias.anchor, -1,
                            0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                break;

            case YAML_SCALAR_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'SCALAR'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"SCALAR", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the scalar anchor. */

                if (input_event.data.scalar.anchor)
                {
                    /* Write 'anchor'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"anchor", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the scalar anchor. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                input_event.data.scalar.anchor, -1,
                                0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Display the scalar tag. */

                if (input_event.data.scalar.tag)
                {
                    /* Write 'tag'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"tag", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the scalar tag. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                input_event.data.scalar.tag, -1,
                                0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Display the scalar value. */

                /* Write 'value'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"value", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write the scalar value. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                            input_event.data.scalar.value,
                            input_event.data.scalar.length,
                            0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display if the scalar tag is implicit. */

                /* Write 'implicit'. */
                
                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"implicit", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write '{'. */

                if (!yaml_mapping_start_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:map", 1,
                            YAML_FLOW_MAPPING_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'plain'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"plain", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write if the scalar is implicit in the plain style. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:bool",
                            (yaml_char_t * )(input_event.data.scalar.plain_implicit ?
                             "true" : "false"), -1,
                            1, 0, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'quoted'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"non-plain", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write if the scalar is implicit in a non-plain style. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:bool",
                            (yaml_char_t *)(input_event.data.scalar.quoted_implicit ?
                             "true" : "false"), -1,
                            1, 0, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write '}'. */

                if (!yaml_mapping_end_event_initialize(&output_event))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the style information. */

                if (input_event.data.scalar.style)
                {
                    yaml_scalar_style_t style = input_event.data.scalar.style;

                    /* Write 'style'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"style", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the scalar style. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                (yaml_char_t *)(style == YAML_PLAIN_SCALAR_STYLE ? "plain" :
                                 style == YAML_SINGLE_QUOTED_SCALAR_STYLE ?
                                        "single-quoted" :
                                 style == YAML_DOUBLE_QUOTED_SCALAR_STYLE ?
                                        "double-quoted" :
                                 style == YAML_LITERAL_SCALAR_STYLE ? "literal" :
                                 style == YAML_FOLDED_SCALAR_STYLE ? "folded" :
                                 "unknown"), -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                break;

            case YAML_SEQUENCE_START_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'SEQUENCE-START'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"SEQUENCE-START", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the sequence anchor. */

                if (input_event.data.sequence_start.anchor)
                {
                    /* Write 'anchor'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"anchor", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the sequence anchor. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                input_event.data.sequence_start.anchor, -1,
                                0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Display the sequence tag. */

                if (input_event.data.sequence_start.tag)
                {
                    /* Write 'tag'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"tag", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the sequence tag. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                input_event.data.sequence_start.tag, -1,
                                0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Write 'implicit'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"implicit", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write if the sequence tag is implicit. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:bool",
                            (yaml_char_t *)(input_event.data.sequence_start.implicit ?
                             "true" : "false"), -1,
                            1, 0, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the style information. */

                if (input_event.data.sequence_start.style)
                {
                    yaml_sequence_style_t style
                        = input_event.data.sequence_start.style;

                    /* Write 'style'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"style", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the scalar style. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                (yaml_char_t *)(style == YAML_BLOCK_SEQUENCE_STYLE ? "block" :
                                 style == YAML_FLOW_SEQUENCE_STYLE ? "flow" :
                                 "unknown"), -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                break;

            case YAML_SEQUENCE_END_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'SEQUENCE-END'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"SEQUENCE-END", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                break;

            case YAML_MAPPING_START_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'MAPPING-START'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"MAPPING-START", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the mapping anchor. */

                if (input_event.data.mapping_start.anchor)
                {
                    /* Write 'anchor'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"anchor", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the mapping anchor. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                input_event.data.mapping_start.anchor, -1,
                                0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Display the mapping tag. */

                if (input_event.data.mapping_start.tag)
                {
                    /* Write 'tag'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"tag", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the mapping tag. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                input_event.data.mapping_start.tag, -1,
                                0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                /* Write 'implicit'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"implicit", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write if the mapping tag is implicit. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:bool",
                            (yaml_char_t *)(input_event.data.mapping_start.implicit ?
                             "true" : "false"), -1,
                            1, 0, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Display the style information. */

                if (input_event.data.mapping_start.style)
                {
                    yaml_mapping_style_t style
                        = input_event.data.mapping_start.style;

                    /* Write 'style'. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"style", -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;

                    /* Write the scalar style. */

                    if (!yaml_scalar_event_initialize(&output_event,
                                NULL, (yaml_char_t *)"tag:yaml.org,2002:str",
                                (yaml_char_t *)(style == YAML_BLOCK_MAPPING_STYLE ? "block" :
                                 style == YAML_FLOW_MAPPING_STYLE ? "flow" :
                                 "unknown"), -1,
                                1, 1, YAML_PLAIN_SCALAR_STYLE))
                        goto event_error;
                    if (!yaml_emitter_emit(&emitter, &output_event))
                        goto emitter_error;
                }

                break;

            case YAML_MAPPING_END_EVENT:

                /* Write 'type'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"type", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                /* Write 'MAPPING-END'. */

                if (!yaml_scalar_event_initialize(&output_event,
                            NULL, (yaml_char_t *)"tag:yaml.org,2002:str", (yaml_char_t *)"MAPPING-END", -1,
                            1, 1, YAML_PLAIN_SCALAR_STYLE))
                    goto event_error;
                if (!yaml_emitter_emit(&emitter, &output_event))
                    goto emitter_error;

                break;

            default:
                /* It couldn't really happen. */
                break;
        }

        /* Delete the event object. */

        yaml_event_delete(&input_event);

        /* Create and emit a MAPPING-END event. */

        if (!yaml_mapping_end_event_initialize(&output_event))
            goto event_error;
        if (!yaml_emitter_emit(&emitter, &output_event))
            goto emitter_error;
    }

    /* Create and emit the SEQUENCE-END event. */

    if (!yaml_sequence_end_event_initialize(&output_event))
        goto event_error;
    if (!yaml_emitter_emit(&emitter, &output_event))
        goto emitter_error;

    /* Create and emit the DOCUMENT-END event. */

    if (!yaml_document_end_event_initialize(&output_event, 0))
        goto event_error;
    if (!yaml_emitter_emit(&emitter, &output_event))
        goto emitter_error;

    /* Create and emit the STREAM-END event. */

    if (!yaml_stream_end_event_initialize(&output_event))
        goto event_error;
    if (!yaml_emitter_emit(&emitter, &output_event))
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
                fprintf(stderr, "Reader error: %s: #%X at %ld\n", parser.problem,
                        parser.problem_value, (long)parser.problem_offset);
            }
            else {
                fprintf(stderr, "Reader error: %s at %ld\n", parser.problem,
                        (long)parser.problem_offset);
            }
            break;

        case YAML_SCANNER_ERROR:
            if (parser.context) {
                fprintf(stderr, "Scanner error: %s at line %d, column %d\n"
                        "%s at line %d, column %d\n", parser.context,
                        (int)parser.context_mark.line+1, (int)parser.context_mark.column+1,
                        parser.problem, (int)parser.problem_mark.line+1,
                        (int)parser.problem_mark.column+1);
            }
            else {
                fprintf(stderr, "Scanner error: %s at line %d, column %d\n",
                        parser.problem, (int)parser.problem_mark.line+1,
                        (int)parser.problem_mark.column+1);
            }
            break;

        case YAML_PARSER_ERROR:
            if (parser.context) {
                fprintf(stderr, "Parser error: %s at line %d, column %d\n"
                        "%s at line %d, column %d\n", parser.context,
                        (int)parser.context_mark.line+1, (int)parser.context_mark.column+1,
                        parser.problem, (int)parser.problem_mark.line+1,
                        (int)parser.problem_mark.column+1);
            }
            else {
                fprintf(stderr, "Parser error: %s at line %d, column %d\n",
                        parser.problem, (int)parser.problem_mark.line+1,
                        (int)parser.problem_mark.column+1);
            }
            break;

        default:
            /* Couldn't happen. */
            fprintf(stderr, "Internal error\n");
            break;
    }

    yaml_event_delete(&input_event);
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
    yaml_parser_delete(&parser);
    yaml_emitter_delete(&emitter);

    return 1;

event_error:

    fprintf(stderr, "Memory error: Not enough memory for creating an event\n");

    yaml_event_delete(&input_event);
    yaml_parser_delete(&parser);
    yaml_emitter_delete(&emitter);

    return 1;
}

