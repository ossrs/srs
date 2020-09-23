/**
* log specified, there must be a log element as:
    <!-- for the log -->
    <div class="alert alert-info fade in" id="txt_log">
        <button type="button" class="close" data-dismiss="alert">×</button>
        <strong><span id="txt_log_title">标题:</span></strong>
        <span id="txt_log_msg">日志内容</span>
    </div>
*/
var srs_log_disabled = false;
function info(desc) {
    if (srs_log_disabled) {
        return;
    }
    
    $("#txt_log").addClass("alert-info").removeClass("alert-error").removeClass("alert-warn");
    $("#txt_log_title").text("Info:");
    $("#txt_log_msg").html(desc);
}
function warn(code, desc) {
    if (srs_log_disabled) {
        return;
    }
    
    $("#txt_log").removeClass("alert-info").removeClass("alert-error").addClass("alert-warn");
    $("#txt_log_title").text("Warn:");
    $("#txt_log_msg").html("code: " + code + ", " + desc);
}
function error(code, desc) {
    if (srs_log_disabled) {
        return;
    }
    
    $("#txt_log").removeClass("alert-info").addClass("alert-error").removeClass("alert-warn");
    $("#txt_log_title").text("Error:");
    $("#txt_log_msg").html("code: " + code + ", " + desc);
}
