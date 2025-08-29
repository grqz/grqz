```http
BREW coffee HTTP/1.1
Host: www.example.re
Content-Type: message/coffeepot
Content-Length: 5

start
```

```http
HTTP/1.1 418 I’m a teapot
Content-Type: text/html
Content-Length: 146

<html>
  <head>
    <title>Beverage not supported</title>
  </head>
  <body>
   <p>I’m a teapot and I don’t support coffee.</p>
  </body>
</html>
```
It's [The Hyper Text Coffee Pot Control Protocol](<https://datatracker.ietf.org/doc/html/rfc7168>)!

