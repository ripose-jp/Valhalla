# Testing

## Overview

Testing uses CTest.
Additional dependencies and configuration steps are documented below.

## Dependencies

* FastCGI
* talloc
* libcurl
* spawn-fcgi
* nginx (or some other webserver configured for FastCGI)

## nginx.conf

These should be in your `server` derective when running tests:
```
location /request {
    include /etc/nginx/fastcgi.conf;
    fastcgi_pass 127.0.0.1:9001;
}
location /response {
    include /etc/nginx/fastcgi.conf;
    fastcgi_pass 127.0.0.1:9002;
}
```
Valhalla assumes the default `fastcgi.conf` (or `fastcgi_params`) configuration
is used. Changing any of these values will lead to undefined behavior.
```
fastcgi_param  SCRIPT_FILENAME    $document_root$fastcgi_script_name;
fastcgi_param  QUERY_STRING       $query_string;
fastcgi_param  REQUEST_METHOD     $request_method;
fastcgi_param  CONTENT_TYPE       $content_type;
fastcgi_param  CONTENT_LENGTH     $content_length;

fastcgi_param  SCRIPT_NAME        $fastcgi_script_name;
fastcgi_param  REQUEST_URI        $request_uri;
fastcgi_param  DOCUMENT_URI       $document_uri;
fastcgi_param  DOCUMENT_ROOT      $document_root;
fastcgi_param  SERVER_PROTOCOL    $server_protocol;
fastcgi_param  REQUEST_SCHEME     $scheme;

fastcgi_param  GATEWAY_INTERFACE  CGI/1.1;
fastcgi_param  SERVER_SOFTWARE    nginx/$nginx_version;

fastcgi_param  REMOTE_ADDR        $remote_addr;
fastcgi_param  REMOTE_PORT        $remote_port;
fastcgi_param  SERVER_ADDR        $server_addr;
fastcgi_param  SERVER_PORT        $server_port;
fastcgi_param  SERVER_NAME        $server_name;
```

## Running

```
mkdir build
cd build
cmake -DBUILD_TESTING=ON ..
make
ctest
```
