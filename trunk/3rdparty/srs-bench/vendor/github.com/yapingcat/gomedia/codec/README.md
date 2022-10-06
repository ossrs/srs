### USAGE

1. 如何解析sps/pps/vps
    以解析sps为例
    ```golang

    //sps原始数据,不带start code(0x00000001)
    var rawsps []byte = []byte{0x67,....}
    
    //step1 创建BitStream
    bs := codec.NewBitStream(rawsps)

    //step2 解析sps
    sps := &SPS{}
    sps.Decode(bs)

    ```
2. 获取视频宽高
    以h264为例子
    ```golang
    //sps原始数据,以startcode开头(0x00000001)
    var rawsps []byte = []byte{0x00,0x00,0x00,0x01,0x67,.....}
    w, h := codec.GetH264Resolution(rawsps)
    ```

3. 生成Extradata
    以h264为例子
    ```golang

    //多个sps原始数据,以startcode开头(0x00000001)
    var spss [][]byte = [][]byte{
        []byte{0x00,0x00,0x00,0x01,0x67,...},
        []byte{0x00,0x00,0x00,0x01,0x67,...},
        ....
    }

     //多个pps原始数据,以startcode开头(0x00000001)
    var ppss [][]byte = [][]byte{
        []byte{0x00,0x00,0x00,0x01,0x68,...},
        []byte{0x00,0x00,0x00,0x01,0x68,...},
        ....
    }
    extranData := codec.CreateH264AVCCExtradata(spss,ppss)
    ```

4. Extradata转为Annex-B格式的sps,pps
    以h264为例子
    ```golang

    //一般从flv/mp4格式中获取 extraData
    //解析出来多个sps,pps, 且sps pps 都以startcode开头
    spss,ppss := codec.CovertExtradata(extraData)
    ```

5. 生成H265 extrandata
    
    ```golang
    // H265的extra data 生成过程稍微复杂一些
    //创建一个 HEVCRecordConfiguration 对象

    hvcc := codec.NewHEVCRecordConfiguration()
    
    //对每一个 sps/pps/vps,调用相应的UpdateSPS,UpdatePPS,UpdateVPS接口
    hvcc.UpdateSPS(sps)
    hvcc.UpdatePPS(pps)
    hvcc.UpdateVPS(vps)

    //调用Encode接口生成
    extran := hvcc.Encode()
    ```
6. 获取对应的sps id/vps id/pps id

    ```golang
    //以h264为例子，有四个接口
    //sps 以startcode 开头
    codec.GetSPSIdWithStartCode(sps)

    //sps2 不以startcode 开头
    codec.GetSPSId(sps2)

    //pps 以startcode 开头
    codec.GetPPSIdWithStartCode(pps)

    //pps2 不以startcode 开头
    codec.GetPPSId(pps2)



    ```
