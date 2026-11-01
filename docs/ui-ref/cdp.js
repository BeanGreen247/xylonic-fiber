// usage: node cdp.js '<js expression>'  -> prints result of Runtime.evaluate (awaitPromise)
const WebSocket=require('/home/bean/xylonic/node_modules/ws');
const http=require('http');
http.get('http://localhost:9333/json',res=>{let d='';res.on('data',c=>d+=c);res.on('end',()=>{
 const t=JSON.parse(d).find(x=>x.type==='page'&&!/mini/i.test(x.url));
 const ws=new WebSocket(t.webSocketDebuggerUrl);
 ws.on('open',()=>ws.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression:process.argv[2],awaitPromise:true,returnByValue:true}})));
 ws.on('message',m=>{const o=JSON.parse(m);if(o.id===1){console.log(JSON.stringify(o.result.result.value??o.result,null,1));ws.close()}});
});});
