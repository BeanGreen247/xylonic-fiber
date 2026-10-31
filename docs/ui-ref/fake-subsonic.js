// Minimal local Subsonic server for measurement: 20 artists x 5 albums x 10 songs, one shared mp3.
const http=require('http'),fs=require('fs'),path=require('path');
const dir=process.argv[2],port=+process.argv[3]||4533;
const mp3=path.join(dir,'media/t1.mp3'),jpg=path.join(dir,'media/cover.jpg');
const A=20,B=5,S=10;
const song=(a,b,s)=>({id:`s${a}-${b}-${s}`,parent:`al${a}-${b}`,title:`Song ${a}-${b}-${s}`,album:`Album ${a}-${b}`,albumId:`al${a}-${b}`,artist:`Artist ${a}`,artistId:`ar${a}`,track:s+1,year:2020,duration:600,bitRate:192,size:fs.statSync(mp3).size,suffix:'mp3',contentType:'audio/mpeg',path:`a${a}/b${b}/${s}.mp3`,coverArt:`al${a}-${b}`,isDir:false,type:'music'});
const alb=(a,b)=>({id:`al${a}-${b}`,name:`Album ${a}-${b}`,artist:`Artist ${a}`,artistId:`ar${a}`,coverArt:`al${a}-${b}`,songCount:S,duration:600*S,year:2020,created:'2026-01-01T00:00:00Z'});
const ok=x=>({'subsonic-response':{status:'ok',version:'1.16.1',type:'fake',serverVersion:'0',openSubsonic:true,...x}});
http.createServer((q,r)=>{
 const u=new URL(q.url,'http://x'),ep=u.pathname.replace(/^\/rest\//,'').replace(/\.view$/,''),p=u.searchParams;
 const j=o=>{r.writeHead(200,{'content-type':'application/json'});r.end(JSON.stringify(ok(o)))};
 if(ep==='stream'||ep==='download'){const st=fs.statSync(mp3),rg=q.headers.range;let s=0,e=st.size-1;if(rg){const m=/bytes=(\d+)-(\d*)/.exec(rg);s=+m[1];if(m[2])e=+m[2];r.writeHead(206,{'content-range':`bytes ${s}-${e}/${st.size}`,'accept-ranges':'bytes','content-length':e-s+1,'content-type':'audio/mpeg'})}else r.writeHead(200,{'accept-ranges':'bytes','content-length':st.size,'content-type':'audio/mpeg'});return fs.createReadStream(mp3,{start:s,end:e}).pipe(r)}
 if(ep==='getCoverArt'){r.writeHead(200,{'content-type':'image/jpeg'});return fs.createReadStream(jpg).pipe(r)}
 switch(ep){
 case'ping':return j({});
 case'getLicense':return j({license:{valid:true}});
 case'getArtists':return j({artists:{ignoredArticles:'',index:[{name:'A',artist:Array.from({length:A},(_,a)=>({id:`ar${a}`,name:`Artist ${a}`,albumCount:B,coverArt:`al${a}-0`}))}]}});
 case'getArtist':{const a=+p.get('id').slice(2);return j({artist:{id:`ar${a}`,name:`Artist ${a}`,albumCount:B,album:Array.from({length:B},(_,b)=>alb(a,b))}})}
 case'getAlbum':{const[a,b]=p.get('id').slice(2).split('-').map(Number);return j({album:{...alb(a,b),song:Array.from({length:S},(_,s)=>song(a,b,s))}})}
 case'getSong':{const[a,b,s]=p.get('id').slice(1).split('-').map(Number);return j({song:song(a,b,s)})}
 case'getAlbumList2':case'getAlbumList':{const n=+p.get('size')||10,o=+p.get('offset')||0,all=[];for(let a=0;a<A;a++)for(let b=0;b<B;b++)all.push(alb(a,b));return j({albumList2:{album:all.slice(o,o+n)},albumList:{album:all.slice(o,o+n)}})}
 case'search3':{const n=+p.get('songCount')||20,o=+p.get('songOffset')||0,all=[];for(let a=0;a<A;a++)for(let b=0;b<B;b++)for(let s=0;s<S;s++)all.push(song(a,b,s));return j({searchResult3:{song:all.slice(o,o+n)}})}
 case'getStarred2':case'getStarred':return j({starred2:{},starred:{}});
 case'getPlaylists':return j({playlists:{}});
 default:return j({});
 }
}).listen(port,'127.0.0.1',()=>console.log('up',port));
