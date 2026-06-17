# Build & test mod_audio_stream against FreeSWITCH 1.10.12
# Builds FreeSWITCH from source on Debian Trixie, then our module

FROM debian:trixie

# Build dependencies: FreeSWITCH + our module
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake libtool libtool-bin cmake g++ make pkg-config \
    libssl-dev libspeex-dev libspeexdsp-dev libopus-dev \
    libncurses-dev libsqlite3-dev libpcre2-dev \
    libcurl4-openssl-dev libedit-dev libldns-dev \
    libsndfile1-dev libtiff-dev libjpeg-dev \
    libspandsp-dev uuid-dev libasound2-dev \
    nasm git \
    wget ca-certificates xz-utils \
    python3 python3-websockets \
    && rm -rf /var/lib/apt/lists/*

# Build pcre from source (Trixie dropped pcre v1, FS 1.10 needs it)
WORKDIR /usr/src
RUN wget -q https://sourceforge.net/projects/pcre/files/pcre/8.45/pcre-8.45.tar.gz \
    && tar xzf pcre-8.45.tar.gz \
    && cd pcre-8.45 \
    && ./configure --prefix=/usr/local --enable-utf8 --enable-unicode-properties \
    && make -j$(nproc) && make install && ldconfig \
    && cd / && rm -rf /usr/src/pcre-8.45

# Build spandsp3 from source (required by FreeSWITCH 1.10.12)
WORKDIR /usr/src
RUN git clone --depth 1 https://github.com/freeswitch/spandsp.git \
    && cd spandsp \
    && ./bootstrap.sh \
    && ./configure --prefix=/usr/local \
    && make -j$(nproc) && make install && ldconfig \
    && cd / && rm -rf /usr/src/spandsp

# Build sofia-sip from source (Trixie package too old for FS 1.10.12)
WORKDIR /usr/src
RUN git clone --depth 1 https://github.com/freeswitch/sofia-sip.git \
    && cd sofia-sip \
    && ./bootstrap.sh \
    && ./configure --prefix=/usr/local \
    && make -j$(nproc) && make install && ldconfig \
    && cd / && rm -rf /usr/src/sofia-sip

# Build FreeSWITCH 1.10.12 from source
WORKDIR /usr/src
RUN wget -q https://github.com/signalwire/freeswitch/archive/refs/tags/v1.10.12.tar.gz \
    && tar xzf v1.10.12.tar.gz \
    && cd freeswitch-1.10.12 \
    && ./bootstrap.sh -j \
    && sed -i -E 's|^(applications/mod_signalwire)|#\1|' modules.conf \
    && sed -i -E 's|^(endpoints/mod_verto)|#\1|' modules.conf \
    && sed -i -E 's|^(applications/mod_av)|#\1|' modules.conf \
    && sed -i -E 's|^(applications/mod_conference)|#\1|' modules.conf \
    && sed -i -E 's|^(languages/mod_lua)|#\1|' modules.conf \
    && sed -i -E 's|^(databases/mod_pgsql)|#\1|' modules.conf \
    && sed -i -E 's|^(applications/mod_spandsp)|#\1|' modules.conf \
    && ./configure --prefix=/usr/local/freeswitch \
        CFLAGS="-Wno-error=stringop-truncation -Wno-error=deprecated-declarations -Wno-error=incompatible-pointer-types" \
    && make -j$(nproc) \
    && make install \
    && make sounds-install \
    && cd / && rm -rf /usr/src/*

# Register FreeSWITCH libraries with the dynamic linker
RUN echo "/usr/local/freeswitch/lib" > /etc/ld.so.conf.d/freeswitch.conf && ldconfig

# Build mod_audio_stream
COPY . /usr/src/mod_audio_stream
WORKDIR /usr/src/mod_audio_stream/build
RUN cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_LOCAL=ON .. \
    && make && make install

# Minimal FS config that loads our module
RUN mkdir -p /usr/local/freeswitch/etc/freeswitch/autoload_configs \
             /usr/local/freeswitch/etc/freeswitch/dialplan
COPY test/integration/freeswitch/autoload_configs/ /usr/local/freeswitch/etc/freeswitch/autoload_configs/
COPY test/integration/freeswitch/dialplan/ /usr/local/freeswitch/etc/freeswitch/dialplan/

ENV PATH="/usr/local/freeswitch/bin:${PATH}"
ENV LD_LIBRARY_PATH="/usr/local/freeswitch/lib"
EXPOSE 8021
CMD ["freeswitch", "-nonat"]
