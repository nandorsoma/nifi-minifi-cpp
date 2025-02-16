# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

# First stage: the build environment
FROM ubuntu:focal AS build_deps
LABEL maintainer="Apache NiFi <dev@nifi.apache.org>"

ARG MINIFI_VERSION

# Install the system dependencies needed for a build

ENV MINIFI_BASE_DIR /opt/minifi
ENV MINIFI_HOME $MINIFI_BASE_DIR/nifi-minifi-cpp-$MINIFI_VERSION
ENV JAVA_HOME /usr/lib/jvm/java-8-openjdk-amd64/

RUN apt update \
    && DEBIAN_FRONTEND="noninteractive" apt install -y openjdk-8-jdk openjdk-8-source python3.9-dev sudo git maven autogen autoconf automake cmake software-properties-common pkgconf libtool \
    && mkdir -p "${MINIFI_BASE_DIR}"

COPY . ${MINIFI_BASE_DIR}


FROM build_deps AS release
ARG ENABLE_JNI

ENV CC gcc-11
ENV CXX g++-11

# Run bootstrap and build
RUN cd $MINIFI_BASE_DIR \
    && ./bootstrap.sh -t \
    && cd $MINIFI_BASE_DIR/build \
    && cmake -DUSE_SHARED_LIBS= -DENABLE_MQTT=ON -DENABLE_LIBRDKAFKA=ON -DPORTABLE=ON -DENABLE_COAP=ON -DENABLE_SQL=ON -DCMAKE_BUILD_TYPE=Release -DSKIP_TESTS=true -DENABLE_JNI=$ENABLE_JNI .. \
    && make -j$(nproc) package
