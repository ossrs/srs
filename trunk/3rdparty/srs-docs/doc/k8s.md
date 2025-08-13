---
title: K8s Guide
sidebar_label: K8s Guide
hide_title: false
hide_table_of_contents: false
---

# K8S

> Cloud+Docker+K8S enable everyone to build live video streaming cluster and service.

Why should you use [k8s](https://docs.kubernetes.io/docs/concepts/overview/what-is-kubernetes) to build your SRS cluster?

* Simple: It's really simple and convenient, let's figure it out by [QuickStart](./k8s.md#quick-start).
* Declarative deployment: We declare a desired SRS cluster and it'll always be there, without starting and migrating service, watchdog and SLB configuration.
* Expand easily: K8S allows you to expand infrastructure automatically, and you can expand your business cluster easily by change the number of Pods.
* Rolling Update: K8S allows deployment update, rollback and gray release with zero downtime.
* XXX: Coming soon...

This tutorial highlights how to build SRS cluster for a variety of scenarios in [ACK(AlibabaCloud Container Service for Kubernetes)](https://www.alibabacloud.com/product/kubernetes).

1. [Deploy to Cloud Platforms](./k8s.md#deploy-to-cloud-platforms): Clone template project and use actions to deploy.
2. [Quick Start](./k8s.md#quick-start): Deployment an SRS origin server in ACK.
3. [SRS Shares Volume with Nginx](./k8s.md#srs-shares-volume-with-nginx): SRS is able to deliver simple HTTP content, or work with Nginx, SRS delivers RTMP/HTTP-FLV and write HLS to a share volume, then Nginx reads and delivers HLS.
4. [SRS Edge Cluster for High Concurrency Streaming](./k8s.md#srs-edge-cluster-with-slb): SRS edge cluster, which is configured and updated automatically, to provide services for huge players.
5. [SRS Origin Cluster for a Large Number of Streams](./k8s.md#srs-origin-cluster-for-a-large-number-of-streams): SRS origin cluster is designed to serve a large number of streams.
6. [SRS Cluster Update, Rollback, Gray Release with Zero Downtime](./k8s.md#srs-cluster-update-rollback-gray-release-with-zero-downtime): K8S allows deployment update, rollback and gray release with zero downtime.
7. [Useful Tips](./k8s.md#useful-tips)
    1. [Create K8S Cluster in ACK](./k8s.md#create-k8s-cluster-in-ack): Create your own k8s cluster in ACK.
    2. [Publish Demo Streams to SRS](./k8s.md#publish-demo-streams-to-srs): Publish the demo streams to SRS.
    3. [Cleanup For DVR/HLS Temporary Files](./k8s.md#cleanup-for-dvrhls-temporary-files): Remove the temporary files for DVR/HLS.
    4. [Use One SLB and EIP for All Streaming Service](./k8s.md#use-one-slb-and-eip-for-all-streaming-service): Use one SLB for RTMP/HTTP-FLV/HLS streaming service.
    5. [Build SRS Origin Cluster as Deployment](./k8s.md#build-srs-origin-cluster-as-deployment): Rather than StatefulSet, we can also use deployment to build Origin Cluster.
    6. [Managing Compute Resources for Containers](./k8s.md#managing-compute-resources-for-containers): Resource requests and limits, and how pods requests are scheduled and limits are run.
    7. [Auto Reload by Inotify](./k8s.md#auto-reload-by-inotify): SRS supports auto reload by inotify watching ConfigMap changes.

## Deploy to Cloud Platforms

SRS provides a set of template repository for fast deploy:

* [General K8s](https://github.com/ossrs/srs-k8s-template)
* [TKE(Tencent Kubernetes Engine)](https://github.com/ossrs/srs-tke-template)
* [ACK(Alibaba Cloud Container Service for Kubernetes)](https://github.com/ossrs/srs-ack-template)
* [EKS(Amazon Elastic Kubernetes Service)](https://github.com/ossrs/srs-eks-template)
* [AKS(Azure Kubernetes Service)](https://github.com/ossrs/srs-aks-template)

## Quick Start

Assumes you have access to a k8s cluster:

```bash
kubectl cluster-info
```

Let's take a look at a single SRS origin server in k8s.

![SRS: Single Origin Server](/img/doc-advanced-guides-k8s-001.png)

**Step 1:** Create a [k8s deployment](https://v1-14.docs.kubernetes.io/docs/concepts/workloads/controllers/deployment) for SRS origin server:

```bash
cat <<EOF | kubectl apply -f -
apiVersion: apps/v1
kind: Deployment
metadata:
  name: srs-deployment
  labels:
    app: srs
spec:
  replicas: 1
  selector:
    matchLabels:
      app: srs
  template:
    metadata:
      labels:
        app: srs
    spec:
      containers:
      - name: srs
        image: ossrs/srs:3
        ports:
        - containerPort: 1935
        - containerPort: 1985
        - containerPort: 8080
EOF
```

**Step 2:** Create a [k8s service](https://v1-14.docs.kubernetes.io/docs/concepts/services-networking/service) which exposing live video streaming service by [SLB](https://www.alibabacloud.com/product/server-load-balancer) with [EIP](https://www.alibabacloud.com/product/eip):

```bash
cat <<EOF | kubectl apply -f -
apiVersion: v1
kind: Service
metadata:
  name: srs-service
spec:
  type: LoadBalancer
  selector:
    app: srs
  ports:
  - name: srs-service-1935-1935
    port: 1935
    protocol: TCP
    targetPort: 1935
  - name: srs-service-1985-1985
    port: 1985
    protocol: TCP
    targetPort: 1985
  - name: srs-service-8080-8080
    port: 8080
    protocol: TCP
    targetPort: 8080
EOF
```

**Step 3:** Done, you could get the EIP and play with SRS now.

Please use `kubectl get svc/srs-service` to get the EIP:

```
NAME          TYPE           CLUSTER-IP      EXTERNAL-IP
srs-service   LoadBalancer   172.21.12.131   28.170.32.118
```

Then you can publish and play with `28.170.32.118`:

* Publish RTMP to `rtmp://28.170.32.118/live/livestream`
* Play RTMP from `rtmp://28.170.32.118/live/livestream`
* Play HTTP-FLV from [http://28.170.32.118:8080/live/livestream.flv](http://ossrs.net/players/srs_player.html?app=live&stream=livestream.flv&server=28.170.32.118&port=8080&autostart=true&vhost=28.170.32.118&schema=http)
* Play HLS from [http://28.170.32.118:8080/live/livestream.m3u8](http://ossrs.net/players/srs_player.html?app=live&stream=livestream.m3u8&server=28.170.32.118&port=8080&autostart=true&vhost=28.170.32.118&schema=http)

![ACK: SRS Done](/img/doc-advanced-guides-k8s-002.png)

## SRS Shares Volume with Nginx

This chapter will show you how SRS is going to provide HTTP Service with Nginx based on K8S.

SRS can distribute RTMP and HTTP-FLV, and write HLS segments to shared Volume, the Nginx Container can read Volume and distribute HLS. Of course SRS can distribute HLS too, but the next scenarios are better to use Nginx :

* 80 port is already used by nginx, SRS can share Volume with Nginx
* Nginx support HTTPS, after configure certificate, it can serve the HLS files generated by SRS through HTTPS protocol.
* Nginx or other Web server, can provide authenticate to serve HLS segments in a secure way.
* SRS now only support a part of HTTP/1.1。 Nginx Open Source version 1.9.5 or higher has built-in support for HTTP/2

the difference of traditional package deployment vs K8S:

|  |ECS|K8S|comment|
|:--|:--|:--|:--|
|resources|Manually|Automatically|From the traditional deployment, the resources SLB、EIP and ECS, you need to buy and configure one by one yourself,  use k8s the mentioned resources can be acquired and configured automatically.|
|deployment|Package|Image|From the K8s deployment, the pod’s docker image can easily rollback, and can keep the dev environment in touch with the prod, and the image can be cached on the node. So with docker image, you will get the required high efficiency、high density、high portability、resource isolation.|
|migration|Manually|Automatically|From the traditional deployment way, when change ECS, you need to apply the new machine resource, modify SLB,  and install application by yourself. Based on K8S, it can auto complete the service migration, update SLB, configure liveness, readiness and startup probes.|

The following architecture of the K8s deployment:
![ack-srs-shares-volume-with-nginx](/img/doc-advanced-guides-k8s-003.png)

Step 1: Create a k8s deployment for SRS and Nginx, the srs container will write the HLS segment to the shared Volume

```
cat <<EOF | kubectl apply -f -
apiVersion: apps/v1
kind: Deployment
metadata:
  name: srs-deploy
  labels:
    app: srs
spec:
  replicas: 1
  selector:
    matchLabels:
      app: srs
  template:
    metadata:
      labels:
        app: srs
    spec:
      volumes:
      - name: cache-volume
        emptyDir: {}
      containers:
      - name: srs
        image: ossrs/srs:3
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 1935
        - containerPort: 1985
        - containerPort: 8080
        volumeMounts:
        - name: cache-volume
          mountPath: /usr/local/srs/objs/nginx/html
          readOnly: false
      - name: nginx
        image: nginx
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 80
        volumeMounts:
        - name: cache-volume
          mountPath: /usr/share/nginx/html
          readOnly: true
      - name: srs-cp-files
        image: ossrs/srs:3
        imagePullPolicy: IfNotPresent
        volumeMounts:
        - name: cache-volume
          mountPath: /tmp/html
          readOnly: false
        command: ["/bin/sh"]
        args:
        - "-c"
        - >
          if [[ ! -f /tmp/html/index.html ]]; then
            cp -R ./objs/nginx/html/* /tmp/html
          fi &&
          sleep infinity
EOF
```

> Note: Nginx’s default directory is /usr/share/nginx/html, please be awared, and change it to your own directory

> Note: To share HLS segments, both SRS and Nginx are mounted to the emptyDir Volume at different paths, the emptyDir volume is initially empty and will be emptied as the pod is destoryed.

> Note: Since the shared emptyDir Volume is initially empty, we start a srs-cp-files container, and copied the SRS default files, please refer to [#1603](https://github.com/ossrs/srs/issues/1603).

Step 2: create a [k8s Service](https://kubernetes.io/docs/concepts/services-networking/service/), using SLB to provide external streaming service.

```
cat <<EOF | kubectl apply -f -
apiVersion: v1
kind: Service
metadata:
  name: srs-origin-service
spec:
  type: LoadBalancer
  selector:
    app: srs
  ports:
  - name: srs-origin-service-80-80
    port: 80
    protocol: TCP
    targetPort: 80
  - name: srs-origin-service-1935-1935
    port: 1935
    protocol: TCP
    targetPort: 1935
  - name: srs-origin-service-1985-1985
    port: 1985
    protocol: TCP
    targetPort: 1985
  - name: srs-origin-service-8080-8080
    port: 8080
    protocol: TCP
    targetPort: 8080
EOF
```

> Note: We expose ports for external services through k8s LoadBalancer Service, where RTMP(1935)/FLV(8080)/API(1985) is served by SRS and HLS(80) is served by Nginx.

> Note: Here we choose ACK to create SLB and EIP automatically, or you can specify SLB manually, refer to [Use One SLB and EIP for All Streaming Service](./k8s.md#ack-srs-buy-slb-eip).

Step 3: Great job. You can publish and play streams now. the HLS stream can by played from SRS(8080) or Nginx(80).
* Publish RTMP to rtmp://28.170.32.118/live/livestream or Publish Demo Streams to SRS.
* Play RTMP from rtmp://28.170.32.118/live/livestream
* Play HTTP-FLV from http://28.170.32.118:8080/live/livestream.flv
* Play HLS from http://28.170.32.118:8080/live/livestream.m3u8
* Play HLS from http://28.170.32.118/live/livestream.m3u8

> Note: Please replace the above EIP with your own, and use 'kubectl get svc/srs-origin-service’ to check your EIP

## SRS Edge Cluster for High Concurrency Streaming

This chapter will show you how to build Edge Cluster for high concurrency streaming based on k8s.

Edge Cluster realizes merging the request of origin source. When there are many players, but request for a same stream, the Edge server still only request one stream from origin. So you can scale up the Edge Cluster to facilitate more clients requests. This is CDN’s  important capability:high concurreny.

> Note: Edge Cluster can be classified as RTMP Edge Cluster or HTTP-FLV Edge Cluster depending on the player’s stream protocol, for more details, can refer to the related Wiki.

For self-host origin, without so many play requests, why is it not recommended to use SRS Single Origin mode, and instead use Edge Cluster mode? look at the related scenarios:

* Avoid overloading of origin. Even if it’s a bit push and play scene, in the case of many CDN requests for one origin stream, may be result in one stream has many request connections. Use Edge cluster can protect origin from too many requests, and transfer the danger to edge.

* Can easily scale up multi Edge Clusters with different SLB exported, and to avoid interference of multiple CDNs,  can execute traffic limit on sperate SLB. Use many Edge Clusters can ensure some CDN is available when Origin is down.

* Can let Edge Cluster to handle stream distribution, and let Origin focus on Slice segments、 DVR、 Authentication functions.

the difference of  traditional package deployment vs K8S:

|          |    ECS   |   K8S   |  comment  |
|  :----:  | :----   | :----  |  :----   |
|   Resources      |       Manually    |Automatically|     From the traditional deployment, the resources SLB、EIP and ECS, you need to buy and configure one by one yourself,  use k8s the mentioned resources can be acquired and configured automatically.     |
|   Deployment      |       Package    |Image|     From the K8s deployment, the pod’s docker image can easily rollback, and can keep the dev environment in touch with the prod, and the image can be cached on the node. <br/>So with docker image, you will get the required high efficiency、high density、high portability、resource isolation.     |
|   Watchdog      |       Manually    |Automatically|     When srs exited abnormally, the event should be monitored and auto restarted, you need do it by yourself from the traditional way.<br/>K8s provides liveness probes and ensuring automatically recovered when anormaly appears.     |
|   Migration      |       Manually    |Automatically|     From the traditional deployment way, when change ECS, you need to apply the new machine resource, modify SLB,  and install application by yourself.<br/>Based on K8S, it can auto complete the service migration, update SLB, configure liveness, readiness and startup probes.     |
|   Configure      |       File    |Volume|     From the traditional deployment way, you need to configure ECS manually.<br/>K8s stores configuration data in ConfigMap, the data can be added to a specific path in the Volume, and consumed by the Pods. Allow you to decouple ECS scale up.     |
|   Scale Up      |       Manually    |Automatically|     From the traditional deployment way, you need to deploy and configure for the new applied ECS. <br/>Based on K8s, just modify the Replicas is ok, you can also enable auto scale.     |
|   Service Discovery      |       Manually    |Automatically|    From the traditional deployment way, when the Origin’s ip changed, you need to configure the new ip in Edge's config file.<br/>Use K8s, it will discover and notify the change to the Edge.      |
|   SLB      |       Manually    |Automatically|    From the traditional deployment way, when add a new Edge server, you need to update SLB config manually.<br/>Based on K8s, it will get updated automatically.      |

The following architecture of The K8s deployment:

![avatar](/img/doc-advanced-guides-k8s-004.png)

Step 1: Create Deployment and Service for SRS origin and Nginx origin.

* srs-origin-deploy: create a [k8s deployment](https://kubernetes.io/docs/concepts/workloads/controllers/deployment/) of stateless application, which contains SRS Server(with origin config)、Nginx containers and a shared volume for containers to mount. The srs container will write the HLS segment to the shared [volume](https://kubernetes.io/docs/concepts/storage/volumes/).

* srs-origin-service: create a k8s ClusterIP [Service](https://kubernetes.io/docs/concepts/services-networking/service/) to provide Origin service, which can only be accessed inside the cluster.

* srs-http-service: create a k8s LoadBalancer [Service](https://kubernetes.io/docs/concepts/services-networking/service/) to provide the SLB based HTTP distribution service of HLS segments powered by Nginx.

```
cat <<EOF | kubectl apply -f -
apiVersion: apps/v1
kind: Deployment
metadata:
  name: srs-origin-deploy
  labels:
    app: srs-origin
spec:
  replicas: 1
  selector:
    matchLabels:
      app: srs-origin
  template:
    metadata:
      labels:
        app: srs-origin
    spec:
      volumes:
      - name: cache-volume
        emptyDir: {}
      containers:
      - name: srs
        image: ossrs/srs:3
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 1935
        - containerPort: 1985
        - containerPort: 8080
        volumeMounts:
        - name: cache-volume
          mountPath: /usr/local/srs/objs/nginx/html
          readOnly: false
      - name: nginx
        image: nginx
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 80
        volumeMounts:
        - name: cache-volume
          mountPath: /usr/share/nginx/html
          readOnly: true
      - name: srs-cp-files
        image: ossrs/srs:3
        imagePullPolicy: IfNotPresent
        volumeMounts:
        - name: cache-volume
          mountPath: /tmp/html
          readOnly: false
        command: ["/bin/sh"]
        args:
        - "-c"
        - >
          if [[ ! -f /tmp/html/index.html ]]; then
            cp -R ./objs/nginx/html/* /tmp/html
          fi &&
          sleep infinity

---

apiVersion: v1
kind: Service
metadata:
  name: srs-origin-service
spec:
  type: ClusterIP
  selector:
    app: srs-origin
  ports:
  - name: srs-origin-service-1935-1935
    port: 1935
    protocol: TCP
    targetPort: 1935

---

apiVersion: v1
kind: Service
metadata:
  name: srs-http-service
spec:
  type: LoadBalancer
  selector:
    app: srs-origin
  ports:
  - name: srs-http-service-80-80
    port: 80
    protocol: TCP
    targetPort: 80
  - name: srs-http-service-1985-1985
    port: 1985
    protocol: TCP
    targetPort: 1985
EOF
```

> Note: The Origin server only can be accessed inside the cluster, for it’s service type is ClsterIP, the Edge Server can connect to the remote Origin Server through the internal domain srs-origin-service.

> Note: For share HLS segments, both SRS and Nginx are mounted to the [emptyDir Volume](https://kubernetes.io/docs/concepts/storage/volumes/#emptydir) at different paths, the emptyDIr volume is initially empty and the data in the emptyDir is deleted when the pod is removed.

> Note: As the emptyDir is initially empty, so we start a srs-cp-files container, which will copy srs’s cached files to the shared volume. please refer[1603](https://github.com/ossrs/srs/issues/1603)

> Note: The srs-http-service provide HLS distribution service with Nginx’s 80 port exported, and provide API service with SRS’s 1985 port exported.

> Note: Here we choose ACK to create SLB and EIP automatically, or you can specify SLB manually, refer to [Use One SLB and EIP for All Streaming Service](./k8s.md#ack-srs-buy-slb-eip)

Step 2: Create Deployment and Service for SRS edge.

* srs-edge-config: create a k8s [ConfigMap](https://kubernetes.io/docs/tasks/configure-pod-container/configure-pod-configmap/), which stores configuration of SRS Edge Server.

* sts-edge-deploy: create a [k8s deployment](https://kubernetes.io/docs/concepts/workloads/controllers/deployment/), which will deploy a stateless application,  and running multi replicas of SRS Edge Server.

* srs-edge-service: create a [k8s Service](https://kubernetes.io/docs/concepts/services-networking/service/), using SLB to provide external streaming service

```
cat <<EOF | kubectl apply -f -
apiVersion: v1
kind: ConfigMap
metadata:
  name: srs-edge-config
data:
  srs.conf: |-
    listen              1935;
    max_connections     1000;
    daemon              off;
    http_api {
        enabled         on;
        listen          1985;
    }
    http_server {
        enabled         on;
        listen          8080;
    }
    vhost __defaultVhost__ {
        cluster {
            mode            remote;
            origin          srs-origin-service;
        }
        http_remux {
            enabled     on;
        }
    }

---

apiVersion: apps/v1
kind: Deployment
metadata:
  name: srs-edge-deploy
  labels:
    app: srs-edge
spec:
  replicas: 3
  selector:
    matchLabels:
      app: srs-edge
  template:
    metadata:
      labels:
        app: srs-edge
    spec:
      volumes:
      - name: config-volume
        configMap:
          name: srs-edge-config
      containers:
      - name: srs
        image: ossrs/srs:3
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 1935
        - containerPort: 1985
        - containerPort: 8080
        volumeMounts:
        - name: config-volume
          mountPath: /usr/local/srs/conf

---

apiVersion: v1
kind: Service
metadata:
  name: srs-edge-service
spec:
  type: LoadBalancer
  selector:
    app: srs-edge
  ports:
  - name: srs-edge-service-1935-1935
    port: 1935
    protocol: TCP
    targetPort: 1935
  - name: srs-edge-service-8080-8080
    port: 8080
    protocol: TCP
    targetPort: 8080
EOF
```

Note: The Edge Server’s configuration is stored in [ConfigMap](https://kubernetes.io/docs/tasks/configure-pod-container/configure-pod-configmap/) of srs-edge-config, and [mount](https://kubernetes.io/docs/concepts/storage/volumes/#configmap) it to the /usr/local/srs/conf path, the srs.conf will appear in the directory.

Note: The Edge Server has been configured to work in cluster mode:remote, it will connect to the Origin Server through domain:srs-origin-service.

Note: The srs-edge-service provide RTMP service with SRS’s 1935 port exported, and provide HTTP-FLV service with SRS’s 80 port.

Note: we choose ACK to create SLB and EIP automatically, or you can specify SLB manually, refer to [Use One SLB and EIP for All Streaming Service](./k8s.md#ack-srs-buy-slb-eip).

Step 3: Now, you made it. you can push retmp stream to the edge, and pull hls stream from Nginx, pull RTMP, HTTP-FLV from SRS.

* Publish RTMP to rtmp://28.170.32.118/live/livestream or [Publish Demo Streams to SRS](./k8s.md#ack-srs-publish-demo-stream-to-edge).

* Play RTMP from `rtmp://28.170.32.118/live/livestream`

* Play HTTP-FLV from [http://28.170.32.118:8080/live/livestream.flv](http://ossrs.net/players/srs_player.html?app=live&stream=livestream.flv&server=28.170.32.118&port=8080&autostart=true&vhost=28.170.32.118&schema=http)

* Play HLS from [http://28.170.32.118/live/livestream.m3u8](http://ossrs.net/players/srs_player.html?app=live&stream=livestream.m3u8&server=28.170.32.118&port=80&autostart=true&vhost=28.170.32.118&schema=http)

> Note: Please change the EIP in the stream address to yourself. you can exec 'kubectl get svc/srs-http-service' or 'kubectl get svc/srs-edge-service’ command to check your EIP address.

> Note: If the SLB and EIP are created automatically, the HLS and RTMP/HTTP-FLV’s EIP are different. you can choose to specify the SLB manually, and both services can use the same SLB, for details please refer to [Use One SLB and EIP for All Streaming Service](./k8s.md#ack-srs-buy-slb-eip).

## SRS Origin Cluster for a Large Number of Streams

Coming soon...

## SRS Cluster Update, Rollback, Gray Release with Zero Downtime

Coming soon...

## Useful Tips

There are some useful tips for you.

1. [Create K8S Cluster in ACK](./k8s.md#create-k8s-cluster-in-ack): Create your own k8s cluster in ACK.
1. [Publish Demo Streams to SRS](./k8s.md#publish-demo-streams-to-srs): Publish the demo streams to SRS.
1. [Use One SLB and EIP for All Streaming Service](./k8s.md#use-one-slb-and-eip-for-all-streaming-service): Use one SLB for RTMP/HTTP-FLV/HLS streaming service.
1. [Build SRS Origin Cluster as Deployment](./k8s.md#build-srs-origin-cluster-as-deployment): Rather than StatefulSet, we can also use deployment to build Origin Cluster.
1. [Managing Compute Resources for Containers](./k8s.md#managing-compute-resources-for-containers): Resource requests and limits, and how pods requests are scheduled and limits are run.
1. [Auto Reload by Inotify](./k8s.md#auto-reload-by-inotify): SRS supports auto reload by inotify watching ConfigMap changes.

### Create K8S Cluster in ACK

Coming soon...

### Publish Demo Streams to SRS

Coming soon...

### Use One SLB for All Streaming Service

Coming soon...

### Build SRS Origin Cluster as Deployment

Coming soon...

### Managing Compute Resources for Containers

Coming soon...

### Auto Reload by Inotify

Coming soon...

Winlin 2020.02

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/k8s)


