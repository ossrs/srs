// for bw to init url
// url: scheme://host:port/path?query#fragment
function srs_init_bwt(rtmp_url, hls_url) {
    update_nav();

    if (rtmp_url) {
		//var query = parse_query_string();
		var search_filed = String(window.location.search).replace(" ", "").split("?")[1];
        $(rtmp_url).val("rtmp://" + window.location.host + ":" + 1935 + "/app?" + search_filed);
    }
    if (hls_url) {
        $(hls_url).val(build_default_hls_url());
    }
}

function srs_bwt_check_url(url) {
	if (url.indexOf("key") != -1 && url.indexOf("vhost") != -1) {
		return true;
	}
	
	return false;
}

function srs_bwt_build_default_url() {
	var url_default = "rtmp://" + window.location.host + ":" + 1935 + "/app?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com";
	return url_default;
}