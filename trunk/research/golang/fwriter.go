package main

import (
	"fmt"
	"os"
	"io"
	"net/http"
)

func main() {
	fmt.Println("FileWriter web server, post to /api/v1/flv to temp.flv")
	http.HandleFunc("/api/v1/flv", func(w http.ResponseWriter, r *http.Request){
		defer r.Body.Close()

		var f *os.File
		var err error
		if f,err = os.OpenFile("temp.flv", os.O_RDWR|os.O_APPEND|os.O_CREATE, os.ModePerm); err != nil {
			return
		}

		var written int64
		written,err = io.Copy(f, r.Body)
		fmt.Println(fmt.Sprintf("write to file temp.flv, %v bytes", written))

		if err != nil {
			return
		}
	})
	_ = http.ListenAndServe(fmt.Sprintf(":8088"), nil)
}