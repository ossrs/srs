/**
* parse the rtmp url,
* for example: rtmp://demo.srs.com:1935/live...vhost...players/livestream
* @return object {server, port, vhost, app, stream}
*/
function srs_parse_rtmp_url(rtmp_url) {
    return parse_rtmp_url(rtmp_url);
}
