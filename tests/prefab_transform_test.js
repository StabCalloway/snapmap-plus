/* Sparse idMat3 -> WebGL transform regression coverage. */
'use strict';
const path=require('path');
const tx=require(path.join(__dirname,'..','src','ui','webview','prefab_transform.js'));
let failures=0;
function check(value,message){if(!value){console.error('[FAIL] '+message);failures++;}}
function near(a,b){return Math.abs(a-b)<1e-5;}
function vecNear(actual,wanted,message){
  check(actual.length===wanted.length && actual.every((v,i)=>near(v,wanted[i])),
        message+' (got '+Array.from(actual).join(', ')+')');
}

const sparse=tx.orientationAxes({mat:{
  'mat[0]':{x:Math.SQRT1_2,z:-Math.SQRT1_2},
  'mat[1]':{x:0},
  'mat[2]':{x:Math.SQRT1_2,z:Math.SQRT1_2}
}});
vecNear(sparse[1],[0,1,0],'missing diagonal components inherit idMat3 identity');

vecNear(tx.vec3({y:115,z:216},[16,128,64]),[16,115,216],
        'saved scale components override rather than erase inherited entityDef dimensions');

const quarterTurn=tx.orientationAxes({mat:{
  'mat[0]':{x:0,y:1}, 'mat[1]':{x:-1,y:0}
}});
const rotated=tx.composeMatrix([10,20,30],quarterTurn,[2,3,4]);
vecNear(tx.transformPoint(rotated,1,0,0),[10,22,30],
        'saved mat[0] is the complete local-X axis, not a transposed row');
vecNear(tx.transformPoint(rotated,0,1,0),[7,20,30],
        'saved mat[1] is the complete local-Y axis');
vecNear(tx.transformPoint(rotated,0,0,1),[10,20,34],
        'omitted mat[2] retains the local-Z identity axis');

const block=tx.composeBottomAnchoredBox([10,20,30],quarterTurn,[2,6,8]);
vecNear(tx.transformPoint(block,0,0,-.5),[10,20,30],
        'centered proxy minimum shares the cooked block bottom origin');
vecNear(tx.transformPoint(block,0,0,.5),[10,20,38],
        'centered proxy maximum preserves the full block height');

const sideways=tx.orientationAxes({mat:{
  'mat[0]':{x:0,z:-1}, 'mat[1]':{y:1}, 'mat[2]':{x:1,z:0}
}});
const sidewaysBlock=tx.composeBottomAnchoredBox([1,2,3],sideways,[4,6,8]);
vecNear(tx.transformPoint(sidewaysBlock,0,0,-.5),[1,2,3],
        'bottom anchoring follows a rotated local-Z axis');
vecNear(tx.transformPoint(sidewaysBlock,0,0,.5),[9,2,3],
        'rotated block dimensions extend along the saved basis axis');

if(failures){console.error('prefab_transform_test: '+failures+' failure(s)');process.exit(1);}
console.log('prefab_transform_test: OK');
