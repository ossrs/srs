---
title: K8s
sidebar_label: K8s
hide_title: false
hide_table_of_contents: false
---

# K8s

> **Note:** SRS K8s is deprecated. Please use [CDK](./getting-started-cdk.md) instead.

We recommend using the HELM method to deploy SRS, see [srs-helm](https://github.com/ossrs/srs-helm). Of course,
SRS also supports direct deployment with K8s, refer to [SRS K8s](./k8s.md).

Actually, HELM is based on K8s and deploys K8s pods, which can be managed with kubectl. However, HELM offers a
more convenient way to manage and install applications, so SRS will mainly support HELM in the future.

Compared to Docker, HELM and K8s are mainly for medium to large scale deployments. If your business is not that
big, we recommend using Docker or Oryx directly. Generally, if you have less than a thousand streams, please
do not use HELM or K8s.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/getting-started-k8s)


