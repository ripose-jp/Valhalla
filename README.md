# Valhalla

## Overview

Valhalla is a simple web framework for developing dynamic websites in C.

## Dependencies

* [FastCGI](https://github.com/FastCGI-Archives/fcgi2)
* [talloc](https://talloc.samba.org/talloc/doc/html/index.html)

## Building

```
mkdir build
cd build
cmake ..
make
sudo make install
```

## Example

```c
#include <valhalla.h>

enum vla_handle_code handler(vla_request *req, void *unused)
{
    vla_response_set_content_type(req, "text/html");
    vla_puts(req, "<h1>Welcome to Valhalla!</h1>");
    vla_printf(req, "<p><b>Your Route:</b> %s</p>", req->document_uri);
    return VLA_HANDLE_RESPOND_ACCEPT;
}

void main()
{
    vla_context *ctx = vla_init();
    vla_add_route(ctx,
        VLA_HTTP_GET, "/*",
        handler, NULL,
        NULL
    );
    vla_accept(ctx);
    vla_free(ctx);
}
```
This code will send all GET requests to the `handler` function which will then
print out **Welcome to Valhalla** followed by **Your Route:** and the URI of the
GET request.

Assuming Valhalla is installed globally, this code can be compiled like such:
```
gcc -lvalhalla main.c -o main
```

Depending on the webserver you're using, FastCGI programs may have to be started
explicitly with a program such as [`spawn-fcgi`](https://github.com/lighttpd/spawn-fcgi).

This can with `spawn-fcgi` like this:
```
spawn-fcgi -p 8000 -n main
```

## Webserver Setup

### nginx

If you wanted to run a Valhalla application on port 8000, you would add this to your `server` derective in your `nginx.conf`:
```
location / {
    include /etc/nginx/fastcgi.conf;
    fastcgi_pass 127.0.0.1:8000;
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


## Documentation

The public API is documented in the [`valhall.h`](https://github.com/ripose-jp/Valhalla/blob/master/src/include/valhalla.h) header.

## Why AGPL?

Valhalla was an idea that spun off of another project I wanted to work on.
In order to decouple boiler plate code for interacting with FastCGI from the web
application, I decided to spin Valhalla off of it.

I intend to relicense Valhalla under LGPL at a later time when Valhalla and the
other project that depends on it are in a more mature state.
The only reason Valhalla is under AGPL is to *keep people away* while it is in
its infancy.
I reserve the right to change the API in breaking ways during this period of
development.
Because of this, I believe AGPL is the perfect way to discourage Valhalla's use
in serious projects at this time, while still encouraging its use in toy
projects.
I believe this is the best way to get user feedback without having to worry
about maintaing a backwards compatible, public API.

## Acknowledgments

* Valhalla bundles in [klib](https://github.com/attractivechaos/klib)'s
  hash table implementation
* Valhalla bundles in the
  [Simple Dynamic Strings](https://github.com/antirez/sds) library.
* Some public domain code by
  [Fred Bulback](https://www.geekhideout.com/urlcode.shtml) used for internal
  URL encoding and decoding.
* FindFCGI cmake script taken from
  [Chromium](https://chromium.googlesource.com/external/github.com/uclouvain/openjpeg/+/refs/heads/master/cmake/FindFCGI.cmake).
* Findtalloc cmake script taken from
  [hhetter](https://github.com/hhetter/smbtad/blob/master/FindTalloc.cmake).
* Unit testing done by [Unity](https://github.com/ThrowTheSwitch/Unity).
