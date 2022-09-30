-- For OpenTelemetry APM protocol over HTTP, see https://github.com/winlinvip/otel-wireshark-plugin
--
-- To apply this wireshark plugin:
--      mkdir -p ~/.local/lib/wireshark/plugins
--      ln -sf $(pwd)/otel.lua ~/.local/lib/wireshark/plugins/otel.lua
-- Download proto files for otel:
--      git clone https://github.com/open-telemetry/opentelemetry-proto.git
-- Setup Wireshark `Protobuf search paths` to load the proto files at `Preferences > Protocols > Protobuf`:
--      /home/winlin/git/opentelemetry-proto
--      /home/winlin/git/otel-wireshark-plugin/cls
-- Start capture or parsing file.

do
  function string_starts_with(str, start)
    return str ~= nil and str:sub(1, #start) == start
  end

  -- See https://gitlab.com/wireshark/wireshark/-/wikis/Protobuf#write-your-own-protobuf-udp-or-tcp-dissectors
  local protobuf_dissector = Dissector.get("protobuf")
  -- Only parsing Protobuf over HTTP, with http uri.
  local f_http_uri = Field.new("http.request.uri")

  local otel_proto = Proto("otel_proto", "Extra analysis of the HTTP protocol");
  function otel_proto.dissector(tvb, pinfo, tree)
    local http_uri = f_http_uri()
    if http_uri == nil then return end

    -- See https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/collector/trace/v1/trace_service.proto
    if string_starts_with(http_uri.value, "/v1/traces") then
      pinfo.private["pb_msg_type"] = "message," .. "opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest"
      pcall(Dissector.call, protobuf_dissector, tvb, pinfo, tree)
    end

    -- See https://cloud.tencent.com/document/api/614/16873
    if string_starts_with(http_uri.value, "/structuredlog") then
        pinfo.private["pb_msg_type"] = "message," .. "cls.LogGroupList"
        pcall(Dissector.call, protobuf_dissector, tvb, pinfo, tree)
    end
  end

  local tbl = DissectorTable.get("media_type")
  tbl:add("application/x-protobuf", otel_proto)
  print("Add application/x-protobuf dissector", otel_proto)
end

