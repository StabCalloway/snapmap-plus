/* Pure SnapMap prefab transform helpers. Pure ASCII; browser global + CommonJS test export. */
var prefabTransform = (function () {
  function finite(value, fallback) {
    value = Number(value);
    return isFinite(value) ? value : fallback;
  }

  function vec3(value, fallback) {
    value = value || {};
    return [finite(value.x, fallback[0]), finite(value.y, fallback[1]),
            finite(value.z, fallback[2])];
  }

  function orientationAxes(value) {
    var source = value && value.mat ? value.mat : null;
    var identity = [[1,0,0],[0,1,0],[0,0,1]], axes = [];
    for (var i = 0; i < 3; i++) {
      var axis = source ? (Array.isArray(source) ? source[i] : source['mat[' + i + ']']) : null;
      if (!axis) axes.push(identity[i].slice());
      else {
        /* Saved entity state is a sparse patch over idMat3's identity default. In particular, an
           unchanged diagonal component can be absent even when another component keeps this axis
           object present. Falling every missing component back to zero can make the matrix singular. */
        axes.push([finite(axis.x, identity[i][0]), finite(axis.y, identity[i][1]),
                   finite(axis.z, identity[i][2])]);
      }
    }
    return axes;
  }

  function composeMatrix(position, axes, scale) {
    /* idMat3 is the one idTech matrix stored column-major: mat[0..2] are the local X/Y/Z axes.
       WebGL is column-major too, so each saved axis becomes one complete column. Transposing the
       component indices here inverts rotations and can lay props or thin blocks on the wrong plane. */
    return new Float32Array([
      axes[0][0]*scale[0], axes[0][1]*scale[0], axes[0][2]*scale[0], 0,
      axes[1][0]*scale[1], axes[1][1]*scale[1], axes[1][2]*scale[1], 0,
      axes[2][0]*scale[2], axes[2][1]*scale[2], axes[2][2]*scale[2], 0,
      position[0], position[1], position[2], 1
    ]);
  }

  function composeBottomAnchoredBox(position, axes, size) {
    /* Cooked SnapMap block/trigger meshes occupy local z=0..1, while the generic proxy cube is
       centered at z=0. Move the proxy center half a local Z axis so fallback and decoded geometry
       share the exact same origin and dimensions. */
    var half = size[2] * .5;
    var center = [position[0] + axes[2][0] * half,
                  position[1] + axes[2][1] * half,
                  position[2] + axes[2][2] * half];
    return composeMatrix(center, axes, size);
  }

  function transformPoint(matrix, x, y, z) {
    return [matrix[0]*x + matrix[4]*y + matrix[8]*z + matrix[12],
            matrix[1]*x + matrix[5]*y + matrix[9]*z + matrix[13],
            matrix[2]*x + matrix[6]*y + matrix[10]*z + matrix[14]];
  }

  return {finite:finite, vec3:vec3, orientationAxes:orientationAxes,
          composeMatrix:composeMatrix, composeBottomAnchoredBox:composeBottomAnchoredBox,
          transformPoint:transformPoint};
})();

if (typeof module !== 'undefined' && module.exports) module.exports = prefabTransform;
