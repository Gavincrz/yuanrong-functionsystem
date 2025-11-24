/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UT_MOCKS_MOCK_KUBE_CLIENT_H
#define UT_MOCKS_MOCK_KUBE_CLIENT_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <async/async.hpp>
#include <nlohmann/json.hpp>

#include "async/future.hpp"
#include "async/option.hpp"
#include "common/logs/logging.h"
#include "common/kube_client/kube_client.h"
#include "common/kube_client/model/common/model_base.h"
#include "function_master/scaler/scaler_actor.h"

namespace functionsystem::test {

const std::string DEPLOYMENT_JSON = R"(
{"status":{"availableReplicas":"0","replicas":"1"},"metadata":{"annotations":{"deployment.kubernetes.io/revision":"2"},"labels":{"app":"function-agent","securityGroup":"manager"},"name":"function-agent","namespace":"default"},"spec":{"progressDeadlineSeconds":600,"replicas":1,"revisionHistoryLimit":10,"selector":{"matchLabels":{"app":"function-agent"}},"strategy":{"rollingUpdate":{"maxSurge":"25%","maxUnavailable":"25%"},"type":"RollingUpdate"},"template":{"metadata":{"labels":{"app":"function-agent"}},"spec":{"containers":[{"command":["/home/sn/bin/entrypoint-runtime-manager"],"env":[{"name":"POD_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.podIP"}}},{"name":"HOST_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.hostIP"}}},{"name":"NODE_ID","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"spec.nodeName"}}},{"name":"RUNTIME_MGR_PORT","value":"21005"},{"name":"FUNCTION_AGENT_PORT","value":"58866"},{"name":"RUNTIME_INIT_PORT","value":"21006"},{"name":"DECRYPT_ALGORITHM","value":"GCM"},{"name":"RUNTIME_PORT_NUM","value":"65535"},{"name":"METRICS_COLLECTOR_TYPE","value":"proc"},{"name":"DISK_USAGE_MONITOR_PATH","value":"/tmp"},{"name":"DISK_USAGE_LIMIT","value":"-1"},{"name":"DISK_USAGE_MONITOR_DURATION","value":"20"},{"name":"CPU4COMP","value":"3000"},{"name":"MEM4COMP","value":"6144"},{"name":"LOG_PATH","value":"/home/sn/log"},{"name":"LOG_LEVEL","value":"INFO"},{"name":"LOG_ROLLING_MAXSIZE","value":"1000"},{"name":"LOG_ROLLING_MAXFILES","value":"3"},{"name":"LOG_ASYNC_LOGBUFSECS","value":"30"},{"name":"LOG_ASYNC_MAXQUEUESIZE","value":"1048510"},{"name":"LOG_ASYNC_THREADCOUNT","value":"1"},{"name":"LOG_ALSOLOGTOSTDERR","value":"false"},{"name":"RUNTIME_LOG_DIR","value":"/home/snuser/log"},{"name":"RUNTIME_LOG_LEVEL","value":"INFO"},{"name":"IS_NEW_RUNTIME_PATH","value":"true"},{"name":"JAVA_PRESTART_COUNT","value":"0"},{"name":"PYTHON_PRESTART_COUNT","value":"0"},{"name":"CPP_PRESTART_COUNT","value":"0"},{"name":"JVM_CUSTOM_ARGS","value":"[]"}],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/runtime-manager:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","livenessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"21005tcp00"},"timeoutSeconds":5},"name":"runtime-manager","ports":[{"containerPort":21005,"name":"21005tcp00","protocol":"TCP"}],"readinessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"21005tcp00"},"timeoutSeconds":5},"resources":{"limits":{"cpu":"3","memory":"6Gi"},"requests":{"cpu":"3","memory":"6Gi"}},"securityContext":{"capabilities":{"add":["SYS_ADMIN"]}},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/etc/localtime","name":"time-zone"},{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"},{"mountPath":"/home/snuser/config/python-runtime-log.json","name":"python-runtime-log-config","readOnly":true,"subPath":"python-runtime-log.json"},{"mountPath":"/home/sn/config/log.json","name":"java-runtime-log-config","subPath":"log.json"},{"mountPath":"/home/snuser/runtime/java/log4j2.xml","name":"java-runtime-log4j2-config","subPath":"log4j2.xml"},{"mountPath":"/home/uds","name":"datasystem-socket"},{"mountPath":"/dev/shm","name":"datasystem-shm"}]},{"command":["/home/sn/bin/entrypoint-function-agent"],"env":[{"name":"POD_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.podIP"}}},{"name":"NODE_ID","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"spec.nodeName"}}},{"name":"HOST_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.hostIP"}}},{"name":"FSPROXY_PORT","value":"22772"},{"name":"FUNCTION_AGENT_PORT","value":"58866"},{"name":"S3_ACCESS_KEY","value":"root"},{"name":"DECRYPT_ALGORITHM","value":"GCM"},{"name":"S3_SECRET_KEY","value":"Huawei@123"},{"name":"S3_ADDR","value":"10.244.134.153:30110"},{"name":"LOG_PATH","value":"/home/sn/log"},{"name":"LOG_LEVEL","value":"INFO"},{"name":"LOG_ROLLING_MAXSIZE","value":"1000"},{"name":"LOG_ROLLING_MAXFILES","value":"3"},{"name":"LOG_ASYNC_LOGBUFSECS","value":"30"},{"name":"LOG_ASYNC_MAXQUEUESIZE","value":"1048510"},{"name":"LOG_ASYNC_THREADCOUNT","value":"1"},{"name":"LOG_ALSOLOGTOSTDERR","value":"false"}],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/function-agent:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","livenessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"58866tcp00"},"timeoutSeconds":5},"name":"function-agent","ports":[{"containerPort":58866,"name":"58866tcp00","protocol":"TCP"}],"readinessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"58866tcp00"},"timeoutSeconds":5},"resources":{"limits":{"cpu":"500m","memory":"500Mi"},"requests":{"cpu":"500m","memory":"500Mi"}},"securityContext":{"capabilities":{"add":["NET_ADMIN","NET_RAW"]}},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/home/sn/resource/rdo/v1/apple","name":"apple","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/boy","name":"boy","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/cat","name":"cat","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/dog","name":"dog","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/egg","name":"egg","readOnly":true},{"mountPath":"/etc/localtime","name":"time-zone"},{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"}]}],"dnsPolicy":"ClusterFirst","imagePullSecrets":[{"name":"default-secret"}],"initContainers":[{"command":["sh","-c","chown 1002:1002 /home/sn/log; chmod 750 /home/sn/log; chown 1002:1002 /dcache; chmod 750 /dcache"],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/runtime-manager:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","name":"function-agent-init","resources":{},"securityContext":{"runAsUser":0},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"}]}],"priorityClassName":"system-cluster-critical","restartPolicy":"Always","schedulerName":"default-scheduler","securityContext":{"fsGroup":1002},"automountServiceAccountToken": false,"terminationGracePeriodSeconds":30,"volumes":[{"hostPath":{"path":"/etc/localtime","type":""},"name":"time-zone"},{"emptyDir":{},"name":"docker-log"},{"emptyDir":{},"name":"pkg-dir"},{"configMap":{"defaultMode":288,"items":[{"key":"a.txt","path":"a.txt"}],"name":"a-config"},"name":"apple"},{"configMap":{"defaultMode":288,"items":[{"key":"b.txt","path":"b.txt"}],"name":"b-config"},"name":"boy"},{"configMap":{"defaultMode":288,"items":[{"key":"c.txt","path":"c.txt"}],"name":"c-config"},"name":"cat"},{"configMap":{"defaultMode":288,"items":[{"key":"d.txt","path":"d.txt"}],"name":"d-config"},"name":"dog"},{"configMap":{"defaultMode":288,"items":[{"key":"e.txt","path":"e.txt"}],"name":"e-config"},"name":"egg"},{"emptyDir":{},"name":"resource-volume"},{"configMap":{"defaultMode":288,"items":[{"key":"python-runtime-log.json","path":"python-runtime-log.json"}],"name":"function-agent-config"},"name":"python-runtime-log-config"},{"configMap":{"defaultMode":288,"items":[{"key":"java-runtime-log.json","path":"log.json"}],"name":"function-agent-config"},"name":"java-runtime-log-config"},{"configMap":{"defaultMode":288,"items":[{"key":"log4j2.xml","path":"log4j2.xml"}],"name":"function-agent-config"},"name":"java-runtime-log4j2-config"},{"hostPath":{"path":"/home/uds","type":""},"name":"datasystem-socket"},{"hostPath":{"path":"/dev/shm","type":""},"name":"datasystem-shm"}]}}}})";
const std::string CREATE_DEPLOYMENT_JSON =
    R"({"metadata":{"annotations":{"deployment.kubernetes.io/revision":"2"},"labels":{"app":"function-agent-600-600-1pool"},"name":"function-agent-600-600-1pool","namespace":"default"},"spec":{"progressDeadlineSeconds":600,"replicas":1,"revisionHistoryLimit":10,"selector":{"matchLabels":{"app":"function-agent-600-600-1pool"}},"strategy":{"rollingUpdate":{"maxSurge":"25%","maxUnavailable":"25%"},"type":"RollingUpdate"},"template":{"metadata":{"labels":{"app":"function-agent-600-600-1pool"}},"spec":{"containers":[{"command":["/home/sn/bin/entrypoint-runtime-manager"],"env":[{"name":"POD_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.podIP"}}},{"name":"HOST_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.hostIP"}}},{"name":"NODE_ID","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"spec.nodeName"}}},{"name":"RUNTIME_MGR_PORT","value":"21005"},{"name":"FUNCTION_AGENT_PORT","value":"58866"},{"name":"RUNTIME_INIT_PORT","value":"21006"},{"name":"DECRYPT_ALGORITHM","value":"GCM"},{"name":"RUNTIME_PORT_NUM","value":"65535"},{"name":"METRICS_COLLECTOR_TYPE","value":"proc"},{"name":"DISK_USAGE_MONITOR_PATH","value":"/tmp"},{"name":"DISK_USAGE_LIMIT","value":"-1"},{"name":"DISK_USAGE_MONITOR_DURATION","value":"20"},{"name":"CPU4COMP","value":"3000"},{"name":"MEM4COMP","value":"6144"},{"name":"LOG_PATH","value":"/home/sn/log"},{"name":"LOG_LEVEL","value":"INFO"},{"name":"LOG_ROLLING_MAXSIZE","value":"1000"},{"name":"LOG_ROLLING_MAXFILES","value":"3"},{"name":"LOG_ASYNC_LOGBUFSECS","value":"30"},{"name":"LOG_ASYNC_MAXQUEUESIZE","value":"1048510"},{"name":"LOG_ASYNC_THREADCOUNT","value":"1"},{"name":"LOG_ALSOLOGTOSTDERR","value":"false"},{"name":"RUNTIME_LOG_DIR","value":"/home/snuser/log"},{"name":"RUNTIME_LOG_LEVEL","value":"INFO"},{"name":"IS_NEW_RUNTIME_PATH","value":"true"},{"name":"JAVA_PRESTART_COUNT","value":"0"},{"name":"PYTHON_PRESTART_COUNT","value":"0"},{"name":"CPP_PRESTART_COUNT","value":"0"},{"name":"JVM_CUSTOM_ARGS","value":"[]"}],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/runtime-manager:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","livenessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"21005tcp00"},"timeoutSeconds":5},"name":"runtime-manager","ports":[{"containerPort":21005,"name":"21005tcp00","protocol":"TCP"}],"readinessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"21005tcp00"},"timeoutSeconds":5},"resources":{"limits":{"cpu":"600m","memory":"600Mi"},"requests":{"cpu":"600m","memory":"600Mi"}},"securityContext":{"capabilities":{"add":["SYS_ADMIN"]}},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/etc/localtime","name":"time-zone"},{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"},{"mountPath":"/home/snuser/config/python-runtime-log.json","name":"python-runtime-log-config","readOnly":true,"subPath":"python-runtime-log.json"},{"mountPath":"/home/sn/config/log.json","name":"java-runtime-log-config","subPath":"log.json"},{"mountPath":"/home/snuser/runtime/java/log4j2.xml","name":"java-runtime-log4j2-config","subPath":"log4j2.xml"},{"mountPath":"/home/uds","name":"datasystem-socket"},{"mountPath":"/dev/shm","name":"datasystem-shm"}]},{"command":["/home/sn/bin/entrypoint-function-agent"],"env":[{"name":"POD_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.podIP"}}},{"name":"NODE_ID","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"spec.nodeName"}}},{"name":"HOST_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.hostIP"}}},{"name":"FSPROXY_PORT","value":"22772"},{"name":"FUNCTION_AGENT_PORT","value":"58866"},{"name":"S3_ACCESS_KEY","value":"root"},{"name":"DECRYPT_ALGORITHM","value":"GCM"},{"name":"S3_SECRET_KEY","value":"Huawei@123"},{"name":"S3_ADDR","value":"10.244.134.153:30110"},{"name":"LOG_PATH","value":"/home/sn/log"},{"name":"LOG_LEVEL","value":"INFO"},{"name":"LOG_ROLLING_MAXSIZE","value":"1000"},{"name":"LOG_ROLLING_MAXFILES","value":"3"},{"name":"LOG_ASYNC_LOGBUFSECS","value":"30"},{"name":"LOG_ASYNC_MAXQUEUESIZE","value":"1048510"},{"name":"LOG_ASYNC_THREADCOUNT","value":"1"},{"name":"LOG_ALSOLOGTOSTDERR","value":"false"}],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/function-agent:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","livenessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"58866tcp00"},"timeoutSeconds":5},"name":"function-agent","ports":[{"containerPort":58866,"name":"58866tcp00","protocol":"TCP"}],"readinessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"58866tcp00"},"timeoutSeconds":5},"resources":{"limits":{"cpu":"500m","memory":"500Mi"},"requests":{"cpu":"500m","memory":"500Mi"}},"securityContext":{"capabilities":{"add":["NET_ADMIN","NET_RAW"]}},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/home/sn/resource/rdo/v1/apple","name":"apple","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/boy","name":"boy","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/cat","name":"cat","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/dog","name":"dog","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/egg","name":"egg","readOnly":true},{"mountPath":"/etc/localtime","name":"time-zone"},{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"}]}],"dnsPolicy":"ClusterFirst","imagePullSecrets":[{"name":"default-secret"}],"initContainers":[{"command":["sh","-c","chown 1002:1002 /home/sn/log; chmod 750 /home/sn/log; chown 1002:1002 /dcache; chmod 750 /dcache"],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/runtime-manager:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","name":"function-agent-init","resources":{},"securityContext":{"runAsUser":0},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"}]}],"priorityClassName":"system-cluster-critical","restartPolicy":"Always","schedulerName":"default-scheduler","securityContext":{"fsGroup":1002},"serviceAccount":"system-function","serviceAccountName":"system-function","terminationGracePeriodSeconds":30,"volumes":[{"hostPath":{"path":"/etc/localtime","type":""},"name":"time-zone"},{"emptyDir":{},"name":"docker-log"},{"emptyDir":{},"name":"pkg-dir"},{"configMap":{"defaultMode":288,"items":[{"key":"a.txt","path":"a.txt"}],"name":"a-config"},"name":"apple"},{"configMap":{"defaultMode":288,"items":[{"key":"b.txt","path":"b.txt"}],"name":"b-config"},"name":"boy"},{"configMap":{"defaultMode":288,"items":[{"key":"c.txt","path":"c.txt"}],"name":"c-config"},"name":"cat"},{"configMap":{"defaultMode":288,"items":[{"key":"d.txt","path":"d.txt"}],"name":"d-config"},"name":"dog"},{"configMap":{"defaultMode":288,"items":[{"key":"e.txt","path":"e.txt"}],"name":"e-config"},"name":"egg"},{"emptyDir":{},"name":"resource-volume"},{"configMap":{"defaultMode":288,"items":[{"key":"python-runtime-log.json","path":"python-runtime-log.json"}],"name":"function-agent-config"},"name":"python-runtime-log-config"},{"configMap":{"defaultMode":288,"items":[{"key":"java-runtime-log.json","path":"log.json"}],"name":"function-agent-config"},"name":"java-runtime-log-config"},{"configMap":{"defaultMode":288,"items":[{"key":"log4j2.xml","path":"log4j2.xml"}],"name":"function-agent-config"},"name":"java-runtime-log4j2-config"},{"hostPath":{"path":"/home/uds","type":""},"name":"datasystem-socket"},{"hostPath":{"path":"/dev/shm","type":""},"name":"datasystem-shm"}]}}}})";
const std::string PATCH_DEPLOYMENT_JSON =
    R"({"metadata":{"annotations":{"deployment.kubernetes.io/revision":"2"},"labels":{"app":"function-agent-600-600-2pool"},"name":"function-agent-600-600-2pool","namespace":"default"},"spec":{"progressDeadlineSeconds":600,"replicas":1,"revisionHistoryLimit":10,"selector":{"matchLabels":{"app":"function-agent-600-600-1pool"}},"strategy":{"rollingUpdate":{"maxSurge":"25%","maxUnavailable":"25%"},"type":"RollingUpdate"},"template":{"metadata":{"labels":{"app":"function-agent-600-600-1pool"}},"spec":{"containers":[{"command":["/home/sn/bin/entrypoint-runtime-manager"],"env":[{"name":"POD_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.podIP"}}},{"name":"HOST_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.hostIP"}}},{"name":"NODE_ID","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"spec.nodeName"}}},{"name":"RUNTIME_MGR_PORT","value":"21005"},{"name":"FUNCTION_AGENT_PORT","value":"58866"},{"name":"RUNTIME_INIT_PORT","value":"21006"},{"name":"DECRYPT_ALGORITHM","value":"GCM"},{"name":"RUNTIME_PORT_NUM","value":"65535"},{"name":"METRICS_COLLECTOR_TYPE","value":"proc"},{"name":"DISK_USAGE_MONITOR_PATH","value":"/tmp"},{"name":"DISK_USAGE_LIMIT","value":"-1"},{"name":"DISK_USAGE_MONITOR_DURATION","value":"20"},{"name":"CPU4COMP","value":"3000"},{"name":"MEM4COMP","value":"6144"},{"name":"LOG_PATH","value":"/home/sn/log"},{"name":"LOG_LEVEL","value":"INFO"},{"name":"LOG_ROLLING_MAXSIZE","value":"1000"},{"name":"LOG_ROLLING_MAXFILES","value":"3"},{"name":"LOG_ASYNC_LOGBUFSECS","value":"30"},{"name":"LOG_ASYNC_MAXQUEUESIZE","value":"1048510"},{"name":"LOG_ASYNC_THREADCOUNT","value":"1"},{"name":"LOG_ALSOLOGTOSTDERR","value":"false"},{"name":"RUNTIME_LOG_DIR","value":"/home/snuser/log"},{"name":"RUNTIME_LOG_LEVEL","value":"INFO"},{"name":"IS_NEW_RUNTIME_PATH","value":"true"},{"name":"JAVA_PRESTART_COUNT","value":"0"},{"name":"PYTHON_PRESTART_COUNT","value":"0"},{"name":"CPP_PRESTART_COUNT","value":"0"},{"name":"JVM_CUSTOM_ARGS","value":"[]"}],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/runtime-manager:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","livenessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"21005tcp00"},"timeoutSeconds":5},"name":"runtime-manager","ports":[{"containerPort":21005,"name":"21005tcp00","protocol":"TCP"}],"readinessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"21005tcp00"},"timeoutSeconds":5},"resources":{"limits":{"cpu":"600m","memory":"600Mi"},"requests":{"cpu":"600m","memory":"600Mi"}},"securityContext":{"capabilities":{"add":["SYS_ADMIN"]}},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/etc/localtime","name":"time-zone"},{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"},{"mountPath":"/home/snuser/config/python-runtime-log.json","name":"python-runtime-log-config","readOnly":true,"subPath":"python-runtime-log.json"},{"mountPath":"/home/sn/config/log.json","name":"java-runtime-log-config","subPath":"log.json"},{"mountPath":"/home/snuser/runtime/java/log4j2.xml","name":"java-runtime-log4j2-config","subPath":"log4j2.xml"},{"mountPath":"/home/uds","name":"datasystem-socket"},{"mountPath":"/dev/shm","name":"datasystem-shm"}]},{"command":["/home/sn/bin/entrypoint-function-agent"],"env":[{"name":"POD_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.podIP"}}},{"name":"NODE_ID","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"spec.nodeName"}}},{"name":"HOST_IP","valueFrom":{"fieldRef":{"apiVersion":"v1","fieldPath":"status.hostIP"}}},{"name":"FSPROXY_PORT","value":"22772"},{"name":"FUNCTION_AGENT_PORT","value":"58866"},{"name":"S3_ACCESS_KEY","value":"root"},{"name":"DECRYPT_ALGORITHM","value":"GCM"},{"name":"S3_SECRET_KEY","value":"Huawei@123"},{"name":"S3_ADDR","value":"10.244.134.153:30110"},{"name":"LOG_PATH","value":"/home/sn/log"},{"name":"LOG_LEVEL","value":"INFO"},{"name":"LOG_ROLLING_MAXSIZE","value":"1000"},{"name":"LOG_ROLLING_MAXFILES","value":"3"},{"name":"LOG_ASYNC_LOGBUFSECS","value":"30"},{"name":"LOG_ASYNC_MAXQUEUESIZE","value":"1048510"},{"name":"LOG_ASYNC_THREADCOUNT","value":"1"},{"name":"LOG_ALSOLOGTOSTDERR","value":"false"}],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/function-agent:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","livenessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"58866tcp00"},"timeoutSeconds":5},"name":"function-agent","ports":[{"containerPort":58866,"name":"58866tcp00","protocol":"TCP"}],"readinessProbe":{"failureThreshold":3,"initialDelaySeconds":10,"periodSeconds":5,"successThreshold":1,"tcpSocket":{"port":"58866tcp00"},"timeoutSeconds":5},"resources":{"limits":{"cpu":"500m","memory":"500Mi"},"requests":{"cpu":"500m","memory":"500Mi"}},"securityContext":{"capabilities":{"add":["NET_ADMIN","NET_RAW"]}},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/home/sn/resource/rdo/v1/apple","name":"apple","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/boy","name":"boy","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/cat","name":"cat","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/dog","name":"dog","readOnly":true},{"mountPath":"/home/sn/resource/rdo/v1/egg","name":"egg","readOnly":true},{"mountPath":"/etc/localtime","name":"time-zone"},{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"}]}],"dnsPolicy":"ClusterFirst","imagePullSecrets":[{"name":"default-secret"}],"initContainers":[{"command":["sh","-c","chown 1002:1002 /home/sn/log; chmod 750 /home/sn/log; chown 1002:1002 /dcache; chmod 750 /dcache"],"image":"cd-docker-hub.szxy5.artifactory.cd-cloud-artifact.tools.huawei.com/yuanrong_euleros_x86/runtime-manager:2.2.0.B060.20230321000714","imagePullPolicy":"IfNotPresent","name":"function-agent-init","resources":{},"securityContext":{"runAsUser":0},"terminationMessagePath":"/dev/termination-log","terminationMessagePolicy":"File","volumeMounts":[{"mountPath":"/home/sn/log","name":"docker-log"},{"mountPath":"/dcache","name":"pkg-dir"}]}],"priorityClassName":"system-cluster-critical","restartPolicy":"Always","schedulerName":"default-scheduler","securityContext":{"fsGroup":1002},"serviceAccount":"system-function","serviceAccountName":"system-function","terminationGracePeriodSeconds":30,"volumes":[{"hostPath":{"path":"/etc/localtime","type":""},"name":"time-zone"},{"emptyDir":{},"name":"docker-log"},{"emptyDir":{},"name":"pkg-dir"},{"configMap":{"defaultMode":288,"items":[{"key":"a.txt","path":"a.txt"}],"name":"a-config"},"name":"apple"},{"configMap":{"defaultMode":288,"items":[{"key":"b.txt","path":"b.txt"}],"name":"b-config"},"name":"boy"},{"configMap":{"defaultMode":288,"items":[{"key":"c.txt","path":"c.txt"}],"name":"c-config"},"name":"cat"},{"configMap":{"defaultMode":288,"items":[{"key":"d.txt","path":"d.txt"}],"name":"d-config"},"name":"dog"},{"configMap":{"defaultMode":288,"items":[{"key":"e.txt","path":"e.txt"}],"name":"e-config"},"name":"egg"},{"emptyDir":{},"name":"resource-volume"},{"configMap":{"defaultMode":288,"items":[{"key":"python-runtime-log.json","path":"python-runtime-log.json"}],"name":"function-agent-config"},"name":"python-runtime-log-config"},{"configMap":{"defaultMode":288,"items":[{"key":"java-runtime-log.json","path":"log.json"}],"name":"function-agent-config"},"name":"java-runtime-log-config"},{"configMap":{"defaultMode":288,"items":[{"key":"log4j2.xml","path":"log4j2.xml"}],"name":"function-agent-config"},"name":"java-runtime-log4j2-config"},{"hostPath":{"path":"/home/uds","type":""},"name":"datasystem-socket"},{"hostPath":{"path":"/dev/shm","type":""},"name":"datasystem-shm"}]}}}})";
const std::string DEFAULT_LEASE_JSON_STR = R"(
{
    "apiVersion": "coordination.k8s.io/v1",
    "kind": "Lease",
    "metadata": {
       "name": "function-master",
       "namespace": "default"
    },
    "spec": {
        "holderIdentity": "10.10.10.10:22770",
        "leaseDurationSeconds": 1000000,
        "leaseTransitions": 100000
    }
}
)";

using namespace functionsystem::kube_client::utility;
using ModelUtils = functionsystem::kube_client::model::ModelUtils;

class MockKubeClient : public functionsystem::KubeClient {
public:
    MockKubeClient()
    {
        YRLOG_INFO("entering mock kube client's constructor...");
        auto kubeClient = std::make_shared<KubeClient>();
        auto apiConf = std::make_shared<ApiConfiguration>();
        auto k8sClient = std::make_shared<ApiClient>(apiConf);
        this->k8sClientApi_ = std::make_shared<K8sClientApi>(k8sClient);
        this->coreClientApi_ = std::make_shared<CoreClientApi>(k8sClient);
        this->coordinationV1Api_ = std::make_shared<CoordinationV1Api>(k8sClient);
    }


    ~MockKubeClient() override = default;

    litebus::Future<std::shared_ptr<V1DeploymentList>> ListNamespacedDeployment(
        const std::string &rNamespace, const litebus::Option<bool> &watch = litebus::None());

    litebus::Future<std::shared_ptr<V1Deployment>> CreateNamespacedDeployment(const std::string &rNamespace,
                                                                              const std::shared_ptr<V1Deployment> &body);

    litebus::Future<std::shared_ptr<V1Deployment>> PatchNamespacedDeployment(const std::string &name,
                                                                             const std::string &rNamespace,
                                                                             const std::shared_ptr<Object> &body);

    MOCK_METHOD(litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>>,
        MOCKListNamespacedHorizontalPodAutoscaler,
        (const std::string, const litebus::Option<bool>));
    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> ListNamespacedHorizontalPodAutoscaler(
        const std::string &rNamespace, const litebus::Option<bool> &watch)
    {
        return MOCKListNamespacedHorizontalPodAutoscaler(rNamespace, watch);
    }

    MOCK_METHOD(litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>>,
        MOCKCreateNamespacedHorizontalPodAutoscaler,
        (const std::string, const std::shared_ptr<V2HorizontalPodAutoscaler>));
    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> CreateNamespacedHorizontalPodAutoscaler(
        const std::string &rNamespace, const std::shared_ptr<V2HorizontalPodAutoscaler> &body)
    {
        return MOCKCreateNamespacedHorizontalPodAutoscaler(rNamespace, body);
    }
    
    MOCK_METHOD(litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>>,
        MOCKPatchNamespacedHorizontalPodAutoscaler,
        (const std::string, const std::string, const std::shared_ptr<Object>));
    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> PatchNamespacedHorizontalPodAutoscaler(
        const std::string &name,
        const std::string &rNamespace,
        const std::shared_ptr<Object> &body)
    {
        return MOCKPatchNamespacedHorizontalPodAutoscaler(name, rNamespace, body);
    }

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Status>>,
        MOCKDeleteNamespacedHorizontalPodAutoscaler,
        (const std::string, const std::string, const litebus::Option<bool>, const litebus::Option<std::shared_ptr<V1DeleteOptions>>));
    litebus::Future<std::shared_ptr<V1Status>> DeleteNamespacedHorizontalPodAutoscaler(
        const std::string &name,
        const std::string &rNamespace,
        const litebus::Option<bool> &orphanDependents,
        const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body)
    {
        return MOCKDeleteNamespacedHorizontalPodAutoscaler(name, rNamespace, orphanDependents, body);
    }

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Deployment>>, DeleteNamespacedDeployment,
                (const std::string &name, const std::string &rNamespace), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1PodList>>, ListNamespacedPod,
                (const std::string &rNamespace, const litebus::Option<bool> &watch), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Pod>>, CreateNamespacedPod,
                (const std::string &rNamespace, const std::shared_ptr<V1Pod> &body), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Pod>>, ReadNamespacedPod,
                (const std::string &rNamespace, const std::string &name), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Pod>>, PatchNamespacedPod,
                (const std::string &name, const std::string &rNamespace, const std::shared_ptr<Object> &body), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Pod>>, DeleteNamespacedPod,
                (const std::string &name, const std::string &rNamespace, const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body,
                 const litebus::Option<bool> &orphanDependents),
                (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Node>>, PatchNode, (const std::string &name, const std::shared_ptr<Object> &body),
                (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1NodeList>>, ListNode, (const litebus::Option<bool> &watch), (override));

    MOCK_METHOD1(MockCreateNamespacedDeployment, void(std::shared_ptr<V1Deployment> body));

    MOCK_METHOD0(MockCreateNamespacedDeployment0, litebus::Future<std::shared_ptr<V1Deployment>>());

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Lease>>, CreateNamespacedLease,
                (const std::string &rNamespace, const std::shared_ptr<V1Lease> &body), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Lease>>, ReadNamespacedLease,
                (const std::string &name, const std::string &rNamespace), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<V1Lease>>, ReplaceNamespacedLease,
                (const std::string &name, const std::string &rNamespace, const std::shared_ptr<V1Lease> &body), (override));

    static std::shared_ptr<V1Lease> CreateLease(const std::string &proposal, const std::string &acquireTime = "",
                                                const std::string &renewTime = "");
    static std::string TimePointToFormatTime(const std::chrono::time_point<std::chrono::system_clock> &time);

protected:
    std::shared_ptr<const scaler::ApiClient> m_apiClient;
    std::shared_ptr<V1Deployment> localDeployment_;
};

inline std::string MockKubeClient::TimePointToFormatTime(const std::chrono::time_point<std::chrono::system_clock> &time)
{
    auto tp = std::chrono::system_clock::to_time_t(time);
    std::stringstream  ss;
    std::put_time(std::localtime(&tp), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

inline std::shared_ptr<V1Lease> MockKubeClient::CreateLease(const std::string &proposal,
                                                            const std::string &acquireTime,
                                                            const std::string &renewTime)
{
    nlohmann::json podJson = nlohmann::json::parse(DEFAULT_LEASE_JSON_STR);
    std::shared_ptr<V1Lease> lease = std::make_shared<V1Lease>();
    lease->FromJson(podJson);
    lease->GetSpec()->SetHolderIdentity(proposal);
    lease->GetSpec()->SetLeaseTransitions(1);
    lease->GetSpec()->SetLeaseDurationSeconds(30);
    if (acquireTime.empty()) {
        lease->GetSpec()->SetAcquireTime(Datetime::FromString(TimePointToFormatTime(std::chrono::system_clock::now())));
    } else {
        lease->GetSpec()->SetAcquireTime(Datetime::FromString(acquireTime));
    }
    if (renewTime.empty()) {
        lease->GetSpec()->SetRenewTime(Datetime::FromString(TimePointToFormatTime(std::chrono::system_clock::now())));
    } else {
        lease->GetSpec()->SetAcquireTime(Datetime::FromString(renewTime));
    }
    return lease;
}

inline litebus::Future<std::shared_ptr<V1DeploymentList>> MockKubeClient::ListNamespacedDeployment(
    const std::string &rNamespace, const litebus::Option<bool> &watch)
{
    YRLOG_INFO("entering mock kube client's ListNamespacedDeployment...");
    litebus::Promise<std::shared_ptr<V1DeploymentList>> promise;
    auto localVarResult = std::make_shared<V1Deployment>();
    nlohmann::json localVarJson = nlohmann::json::parse(DEPLOYMENT_JSON);

    auto converted = localVarResult->FromJson(localVarJson);
    if (!converted) {
        YRLOG_ERROR("failed to convert deployment");
    }
    auto normalPool1 = std::make_shared<V1Deployment>();
    normalPool1->FromJson(localVarJson);
    normalPool1->GetMetadata()->SetName("function-agent-pool22-300-128-fusion");
    auto dynamicPool1 = std::make_shared<V1Deployment>();
    dynamicPool1->FromJson(localVarJson);
    dynamicPool1->GetMetadata()->SetName("function-agent-pool2");
    dynamicPool1->GetMetadata()->GetLabels()["group2"] = "group2";
    auto otherDeployment = std::make_shared<V1Deployment>();
    otherDeployment->FromJson(localVarJson);
    otherDeployment->GetMetadata()->SetName("non-function-agent-pool");
    std::vector<std::shared_ptr<V1Deployment>> deploymentsList{ localVarResult, normalPool1, dynamicPool1, otherDeployment };
    auto v1DeploymentList = std::make_shared<V1DeploymentList>();
    v1DeploymentList->SetItems(deploymentsList);
    promise.SetValue(v1DeploymentList);
    return promise.GetFuture();
}

inline litebus::Future<std::shared_ptr<V1Deployment>> MockKubeClient::CreateNamespacedDeployment(
    const std::string &rNamespace, const std::shared_ptr<V1Deployment> &body)
{
    YRLOG_INFO("entering mock kube client's CreateNamespacedDeployment...");
    if (rNamespace == "mock") {
        return MockCreateNamespacedDeployment0();
    }
    MockCreateNamespacedDeployment(body);
    litebus::Promise<std::shared_ptr<V1Deployment>> promise;
    std::shared_ptr<V1Deployment> localVarResult(new V1Deployment());
    nlohmann::json localVarJson = nlohmann::json::parse(CREATE_DEPLOYMENT_JSON);
    ModelUtils::FromJson(localVarJson, localVarResult);
    promise.SetValue(localVarResult);
    return promise.GetFuture();
}

inline litebus::Future<std::shared_ptr<V1Deployment>> MockKubeClient::PatchNamespacedDeployment(
    const std::string &name, const std::string &rNamespace, const std::shared_ptr<Object> &body) {
    YRLOG_INFO("entering mock kube client's PatchNamespacedDeployment...");
    litebus::Promise<std::shared_ptr<V1Deployment>> promise;
    std::shared_ptr<V1Deployment> localVarResult(new V1Deployment());
    nlohmann::json localVarJson = nlohmann::json::parse(PATCH_DEPLOYMENT_JSON);

    ModelUtils::FromJson(localVarJson, localVarResult);
    promise.SetValue(localVarResult);
    return promise.GetFuture();
}

}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_KUBE_CLIENT_H
