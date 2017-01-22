/*
 * vim: set ft=rust:
 * vim: set ft=reason:
 */
open Common;

open Glloader;

open Utils;

let getProgram
    gl::(gl: Gl.contextT)
    vertexShader::(vertexShaderSource: string)
    fragmentShader::(fragmentShaderSource: string)
    :option Gl.programT => {
  let vertexShader = Gl.createShader gl Constants.vertex_shader;
  Gl.shaderSource gl vertexShader vertexShaderSource;
  Gl.compileShader gl vertexShader;
  let compiledCorrectly =
    Gl.getShaderParameter context::gl shader::vertexShader paramName::Gl.Compile_status == 1;
  if compiledCorrectly {
    let fragmentShader = Gl.createShader gl Constants.fragment_shader;
    Gl.shaderSource gl fragmentShader fragmentShaderSource;
    Gl.compileShader gl fragmentShader;
    let compiledCorrectly =
      Gl.getShaderParameter context::gl shader::fragmentShader paramName::Gl.Compile_status == 1;
    if compiledCorrectly {
      let program = Gl.createProgram gl;
      Gl.attachShader context::gl ::program shader::vertexShader;
      Gl.deleteShader context::gl shader::vertexShader;
      Gl.attachShader context::gl ::program shader::fragmentShader;
      Gl.deleteShader context::gl shader::fragmentShader;
      Gl.linkProgram gl program;
      let linkedCorrectly =
        Gl.getProgramParameter context::gl ::program paramName::Gl.Link_status == 1;
      if linkedCorrectly {
        Some program
      } else {
        print_endline @@ "Linking error: " ^ Gl.getProgramInfoLog context::gl ::program;
        None
      }
    } else {
      print_endline @@
      "Fragment shader error: " ^ Gl.getShaderInfoLog context::gl shader::fragmentShader;
      None
    }
  } else {
    print_endline @@
    "Vertex shader error: " ^ Gl.getShaderInfoLog context::gl shader::vertexShader;
    None
  }
};

let createCanvas window (height: int) (width: int) :glEnv => {
  Gl.Window.setWindowSize ::window ::width ::height;
  let gl = Gl.Window.getContext window;
  Gl.viewport context::gl x::(-1) y::(-1) ::width ::height;
  Gl.clearColor context::gl r::0. g::0. b::0. a::1.;
  Gl.clear context::gl mask::(Constants.color_buffer_bit lor Constants.depth_buffer_bit);

  /** Camera is a simple record containing one matrix used to project a point in 3D onto the screen. **/
  let camera = {projectionMatrix: Gl.Mat4.create ()};
  let vertexBuffer = Gl.createBuffer gl;
  let elementBuffer = Gl.createBuffer gl;
  let program =
    switch (
      getProgram
        ::gl vertexShader::Shaders.vertexShaderSource fragmentShader::Shaders.fragmentShaderSource
    ) {
    | None => failwith "Could not create the program and/or the shaders. Aborting."
    | Some program => program
    };
  Gl.useProgram gl program;

  /** Get the attribs ahead of time to be used inside the render function **/
  let aVertexPosition = Gl.getAttribLocation context::gl ::program name::"aVertexPosition";
  Gl.enableVertexAttribArray context::gl attribute::aVertexPosition;
  let aVertexColor = Gl.getAttribLocation context::gl ::program name::"aVertexColor";
  Gl.enableVertexAttribArray context::gl attribute::aVertexColor;
  let pMatrixUniform = Gl.getUniformLocation gl program "uPMatrix";
  Gl.uniformMatrix4fv context::gl location::pMatrixUniform value::camera.projectionMatrix;

  /** Get attribute and uniform locations for later usage in the draw code. **/
  let aTextureCoord = Gl.getAttribLocation context::gl ::program name::"aTextureCoord";
  Gl.enableVertexAttribArray context::gl attribute::aTextureCoord;

  /** Generate texture buffer that we'll use to pass image data around. **/
  let texture = Gl.createTexture gl;

  /** This tells OpenGL that we're going to be using texture0. OpenGL imposes a limit on the number of
      texture we can manipulate at the same time. That limit depends on the device. We don't care as we'll just
      always use texture0. **/
  Gl.activeTexture context::gl target::Constants.texture0;

  /** Bind `texture` to `texture_2d` to modify it's magnification and minification params. **/
  Gl.bindTexture context::gl target::Constants.texture_2d ::texture;

  /** Tell OpenGL about what the uniform called `uSampler` is pointing at, here it's given 0 which is what
      texture0 represent.  uSampler is set once here and never changed.  **/
  let uSampler = Gl.getUniformLocation gl program "uSampler";
  Gl.uniform1i context::gl location::uSampler 0;

  /** Load a dummy texture. This is because we're using the same shader for things with and without a texture */
  Gl.texImage2D
    context::gl
    target::Constants.texture_2d
    level::0
    internalFormat::Constants.rgba
    width::1
    height::1
    format::Constants.rgba
    type_::Constants.unsigned_byte
    data::(Gl.toTextureData [|0, 0, 0, 0|]);
  Gl.texParameteri
    context::gl
    target::Constants.texture_2d
    pname::Constants.texture_mag_filter
    param::Constants.linear;
  Gl.texParameteri
    context::gl
    target::Constants.texture_2d
    pname::Constants.texture_min_filter
    param::Constants.linear_mipmap_nearest;

  /** Enable blend and tell OpenGL how to blend. */
  Gl.enable context::gl Constants.blend;
  Gl.blendFunc context::gl Constants.src_alpha Constants.one_minus_src_alpha;

  /**
   * Will mutate the projectionMatrix to be an ortho matrix with the given boundaries.
   * See this link for quick explanation of what this is.
   * https://shearer12345.github.io/graphics/assets/projectionPerspectiveVSOrthographic.png
   */
  Gl.Mat4.ortho
    out::camera.projectionMatrix
    left::0.
    right::(float_of_int width)
    bottom::(float_of_int height)
    top::0.
    near::0.
    far::1.;
  let currFill = {r: 0, g: 0, b: 0};
  let currBackground = {r: 0, g: 0, b: 0};
  {
    camera,
    window,
    gl,
    batch: {
      vertexArray: Gl.Bigarray.create Gl.Bigarray.Float32 (circularBufferSize * vertexSize),
      elementArray: Gl.Bigarray.create Gl.Bigarray.Uint16 circularBufferSize,
      vertexPtr: 0,
      elementPtr: 0,
      currTex: None,
      nullTex: texture
    },
    vertexBuffer,
    elementBuffer,
    aVertexPosition,
    aTextureCoord,
    aVertexColor,
    pMatrixUniform,
    uSampler,
    currFill,
    currBackground,
    mouse: {pos: (0, 0), prevPos: (0, 0), pressed: false},
    stroke: {color: {r: 0, g: 0, b: 0}, weight: 10},
    frame: {count: 1, rate: 10},
    size: {height, width, resizeable: true}
  }
};

let drawGeometry
    vertexArray::(vertexArray: Gl.Bigarray.t float Gl.Bigarray.float32_elt)
    elementArray::(elementArray: Gl.Bigarray.t int Gl.Bigarray.int16_unsigned_elt)
    ::mode
    ::count
    ::textureBuffer
    env => {
  /* Bind `vertexBuffer`, a pointer to chunk of memory to be sent to the GPU to the "register" called
     `array_buffer` */
  Gl.bindBuffer context::env.gl target::Constants.array_buffer buffer::env.vertexBuffer;

  /** Copy all of the data over into whatever's in `array_buffer` (so here it's `vertexBuffer`) **/
  Gl.bufferData
    context::env.gl target::Constants.array_buffer data::vertexArray usage::Constants.stream_draw;

  /** Tell the GPU about the shader attribute called `aVertexPosition` so it can access the data per vertex */
  Gl.vertexAttribPointer
    context::env.gl
    attribute::env.aVertexPosition
    size::2
    type_::Constants.float_
    normalize::false
    stride::(vertexSize * 4)
    offset::0;

  /** Same as above but for `aVertexColor` **/
  Gl.vertexAttribPointer
    context::env.gl
    attribute::env.aVertexColor
    size::4
    type_::Constants.float_
    normalize::false
    stride::(vertexSize * 4)
    offset::(2 * 4);

  /** Same as above but for `aTextureCoord` **/
  Gl.vertexAttribPointer
    context::env.gl
    attribute::env.aTextureCoord
    size::2
    type_::Constants.float_
    normalize::false
    stride::(vertexSize * 4)
    offset::(6 * 4);

  /** Bind `elementBuffer`, a pointer to GPU memory to `element_array_buffer`. That "register" is used for
      the data representing the indices of the vertex. **/
  Gl.bindBuffer context::env.gl target::Constants.element_array_buffer buffer::env.elementBuffer;

  /** Copy the `elementArray` into whatever buffer is in `element_array_buffer` **/
  Gl.bufferData
    context::env.gl
    target::Constants.element_array_buffer
    data::elementArray
    usage::Constants.stream_draw;

  /** We bind `texture` to texture_2d, like we did for the vertex buffers in some ways (I think?) **/
  Gl.bindTexture context::env.gl target::Constants.texture_2d texture::textureBuffer;

  /** Final call which actually tells the GPU to draw. **/
  Gl.drawElements context::env.gl ::mode ::count type_::Constants.unsigned_short offset::0
};

/*
 * Helper that will send the currently available data inside globalVertexArray.
 * This function assumes that the vertex data is stored as simple triangles.
 *
 * That function creates a new big array with a new size given the offset and len but does NOT copy the
 * underlying array of memory. So mutation done to that sub array will be reflected in the original one.
 */
let flushGlobalBatch env =>
  if ((!env).batch.elementPtr > 0) {
    let textureBuffer =
      switch (!env).batch.currTex {
      | None => (!env).batch.nullTex
      | Some textureBuffer => textureBuffer
      };
    drawGeometry
      vertexArray::(Gl.Bigarray.sub (!env).batch.vertexArray offset::0 len::(!env).batch.vertexPtr)
      elementArray::(
        Gl.Bigarray.sub (!env).batch.elementArray offset::0 len::(!env).batch.elementPtr
      )
      mode::Constants.triangles
      count::(!env).batch.elementPtr
      ::textureBuffer
      !env;
    (!env).batch.currTex = None;
    (!env).batch.vertexPtr = 0;
    (!env).batch.elementPtr = 0
  };

let maybeFlushBatch env texture adding =>
  if (
    (!env).batch.elementPtr + adding >= circularBufferSize ||
    (!env).batch.elementPtr > 0 && (!env).batch.currTex !== texture
  ) {
    flushGlobalBatch env
  };

/*
 * This array packs all of the values that the shaders need: vertices, colors and texture coordinates.
 * We put them all in one as an optimization, so there are less back and forths between us and the GPU.
 *
 * The vertex array looks like:
 *
 * |<--------  8 * 4 bytes  ------->|
 *  --------------------------------
 * |  x  y  |  r  g  b  a  |  s  t  |  x2  y2  |  r2  g2  b2  a2  |  s2  t2  | ....
 *  --------------------------------
 * |           |              |
 * +- offset: 0 bytes, stride: 8 * 4 bytes (because we need to move by 8*4 bytes to get to the next x)
 *             |              |
 *             +- offset: 3 * 4 bytes, stride: 8 * 4 bytes
 *                            |
 *                            +- offset: (3 + 4) * 4 bytes, stride: 8 * 4 bytes
 *
 *
 * The element array is just an array of indices of vertices given that each vertex takes 8 * 4 bytes.
 * For example, if the element array looks like [|0, 1, 2, 1, 2, 3|], we're telling the GPU to draw 2
 * triangles: one with the vertices 0, 1 and 2 from the vertex array, and one with the vertices 1, 2 and 3.
 * We can "point" to duplicated vertices in our geometry to avoid sending those vertices.
 */
let addRectToGlobalBatch env (x1, y1) (x2, y2) (x3, y3) (x4, y4) {r, g, b} => {
  maybeFlushBatch env None 6;
  let set = Gl.Bigarray.set;
  let toColorFloat i => float_of_int i /. 255.;
  let (r, g, b) = (toColorFloat r, toColorFloat g, toColorFloat b);
  let i = (!env).batch.vertexPtr;
  let vertexArrayToMutate = (!env).batch.vertexArray;
  set vertexArrayToMutate (i + 0) x1;
  set vertexArrayToMutate (i + 1) y1;
  set vertexArrayToMutate (i + 2) r;
  set vertexArrayToMutate (i + 3) g;
  set vertexArrayToMutate (i + 4) b;
  set vertexArrayToMutate (i + 5) 1.;
  set vertexArrayToMutate (i + 6) 0.0;
  set vertexArrayToMutate (i + 7) 0.0;
  set vertexArrayToMutate (i + 8) x2;
  set vertexArrayToMutate (i + 9) y2;
  set vertexArrayToMutate (i + 10) r;
  set vertexArrayToMutate (i + 11) g;
  set vertexArrayToMutate (i + 12) b;
  set vertexArrayToMutate (i + 13) 1.;
  set vertexArrayToMutate (i + 14) 0.0;
  set vertexArrayToMutate (i + 15) 0.0;
  set vertexArrayToMutate (i + 16) x3;
  set vertexArrayToMutate (i + 17) y3;
  set vertexArrayToMutate (i + 18) r;
  set vertexArrayToMutate (i + 19) g;
  set vertexArrayToMutate (i + 20) b;
  set vertexArrayToMutate (i + 21) 1.;
  set vertexArrayToMutate (i + 22) 0.0;
  set vertexArrayToMutate (i + 23) 0.0;
  set vertexArrayToMutate (i + 24) x4;
  set vertexArrayToMutate (i + 25) y4;
  set vertexArrayToMutate (i + 26) r;
  set vertexArrayToMutate (i + 27) g;
  set vertexArrayToMutate (i + 28) b;
  set vertexArrayToMutate (i + 29) 1.;
  set vertexArrayToMutate (i + 30) 0.0;
  set vertexArrayToMutate (i + 31) 0.0;
  let ii = i / vertexSize;
  let j = (!env).batch.elementPtr;
  let elementArrayToMutate = (!env).batch.elementArray;
  set elementArrayToMutate (j + 0) ii;
  set elementArrayToMutate (j + 1) (ii + 1);
  set elementArrayToMutate (j + 2) (ii + 2);
  set elementArrayToMutate (j + 3) (ii + 1);
  set elementArrayToMutate (j + 4) (ii + 2);
  set elementArrayToMutate (j + 5) (ii + 3);
  (!env).batch.vertexPtr = i + 4 * vertexSize;
  (!env).batch.elementPtr = j + 6
};

let drawEllipseInternal env xCenterOfCircle yCenterOfCircle radx rady => {
  let noOfFans = (radx + rady) * 2;
  maybeFlushBatch env (Some (!env).batch.nullTex) ((noOfFans - 3) * 3 + 3);
  let pi = 4.0 *. atan 1.0;
  let anglePerFan = 2. *. pi /. float_of_int noOfFans;
  let radxf = float_of_int radx;
  let radyf = float_of_int rady;
  let toColorFloat i => float_of_int i /. 255.;
  let (r, g, b) = (
    toColorFloat (!env).currFill.r,
    toColorFloat (!env).currFill.g,
    toColorFloat (!env).currFill.b
  );
  let xCenterOfCirclef = float_of_int xCenterOfCircle;
  let yCenterOfCirclef = float_of_int yCenterOfCircle;
  let verticesData = (!env).batch.vertexArray;
  let elementData = (!env).batch.elementArray;
  let set = Gl.Bigarray.set;
  let get = Gl.Bigarray.get;
  let vertexArrayOffset = (!env).batch.vertexPtr;
  let elementArrayOffset = (!env).batch.elementPtr;
  for i in 0 to (noOfFans - 1) {
    let angle = anglePerFan *. float_of_int (i + 1);
    let xCoordinate = xCenterOfCirclef +. cos angle *. radxf;
    let yCoordinate = yCenterOfCirclef +. sin angle *. radyf;
    let ii = i * vertexSize + vertexArrayOffset;
    set verticesData (ii + 0) xCoordinate;
    set verticesData (ii + 1) yCoordinate;
    set verticesData (ii + 2) r;
    set verticesData (ii + 3) g;
    set verticesData (ii + 4) b;
    set verticesData (ii + 5) 1.0;
    set verticesData (ii + 6) 0.0;
    set verticesData (ii + 7) 0.0;
    /* For the first three vertices, we don't do any deduping. Then for the subsequent ones, we'll actually
       have 3 elements, one pointing at the first vertex, one pointing at the previously added vertex and one
       pointing at the current vertex. This mimicks the behavior of triangle_fan. */
    if (i < 3) {
      set elementData (i + elementArrayOffset) (ii / vertexSize)
    } else {
      /* We've already added 3 elements, for i = 0, 1 and 2. From now on, we'll add 3 elements _per_ i.
         To calculate the correct offset in `elementData` we remove 3 from i as if we're starting from 0 (the
         first time we enter this loop i = 3), then for each i we'll add 3 elements (so multiply by 3) BUT for
         i = 3 we want `jj` to be 3 so we shift everything by 3 (so add 3). Everything's also shifted by
         `elementArrayOffset` */
      let jj = (i - 3) * 3 + elementArrayOffset + 3;
      set elementData jj (vertexArrayOffset / vertexSize);
      set elementData (jj + 1) (get elementData (jj - 1));
      set elementData (jj + 2) (ii / vertexSize)
    }
  };
  (!env).batch.vertexPtr = (!env).batch.vertexPtr + noOfFans * vertexSize;
  (!env).batch.elementPtr = (!env).batch.elementPtr + (noOfFans - 3) * 3 + 3
};

let loadImage (env: ref glEnv) filename :imageT => {
  let imageRef = ref None;
  Gl.loadImage
    ::filename
    callback::(
      fun imageData =>
        switch imageData {
        | None => failwith ("Could not load image '" ^ filename ^ "'.") /* TODO: handle this better? */
        | Some img =>
          let env = !env;
          let textureBuffer = Gl.createTexture context::env.gl;
          let height = Gl.getImageHeight img;
          let width = Gl.getImageWidth img;
          imageRef := Some {img, textureBuffer, height, width};
          Gl.bindTexture context::env.gl target::Constants.texture_2d texture::textureBuffer;
          Gl.texImage2DWithImage context::env.gl target::Constants.texture_2d level::0 image::img;
          Gl.texParameteri
            context::env.gl
            target::Constants.texture_2d
            pname::Constants.texture_mag_filter
            param::Constants.linear;
          Gl.texParameteri
            context::env.gl
            target::Constants.texture_2d
            pname::Constants.texture_min_filter
            param::Constants.linear
        }
    )
    ();
  imageRef
};

let drawImageInternal {width, height, textureBuffer} ::x ::y ::subx ::suby ::subw ::subh env => {
  maybeFlushBatch env (Some textureBuffer) 6;
  let (fsubx, fsuby, fsubw, fsubh) = (
    float_of_int subx /. float_of_int width,
    float_of_int suby /. float_of_int height,
    float_of_int subw /. float_of_int width,
    float_of_int subh /. float_of_int height
  );
  let (x1, y1) = (float_of_int @@ x + subw, float_of_int @@ y + subh);
  let (x2, y2) = (float_of_int x, float_of_int @@ y + subh);
  let (x3, y3) = (float_of_int @@ x + subw, float_of_int y);
  let (x4, y4) = (float_of_int x, float_of_int y);
  let set = Gl.Bigarray.set;
  let ii = (!env).batch.vertexPtr;
  let vertexArray = (!env).batch.vertexArray;
  set vertexArray (ii + 0) x1;
  set vertexArray (ii + 1) y1;
  set vertexArray (ii + 2) 0.0;
  set vertexArray (ii + 3) 0.0;
  set vertexArray (ii + 4) 0.0;
  set vertexArray (ii + 5) 0.0;
  set vertexArray (ii + 6) (fsubx +. fsubw);
  set vertexArray (ii + 7) (fsuby +. fsubh);
  set vertexArray (ii + 8) x2;
  set vertexArray (ii + 9) y2;
  set vertexArray (ii + 10) 0.0;
  set vertexArray (ii + 11) 0.0;
  set vertexArray (ii + 12) 0.0;
  set vertexArray (ii + 13) 0.0;
  set vertexArray (ii + 14) fsubx;
  set vertexArray (ii + 15) (fsuby +. fsubh);
  set vertexArray (ii + 16) x3;
  set vertexArray (ii + 17) y3;
  set vertexArray (ii + 18) 0.0;
  set vertexArray (ii + 19) 0.0;
  set vertexArray (ii + 20) 0.0;
  set vertexArray (ii + 21) 0.0;
  set vertexArray (ii + 22) (fsubx +. fsubw);
  set vertexArray (ii + 23) fsuby;
  set vertexArray (ii + 24) x4;
  set vertexArray (ii + 25) y4;
  set vertexArray (ii + 26) 0.0;
  set vertexArray (ii + 27) 0.0;
  set vertexArray (ii + 28) 0.0;
  set vertexArray (ii + 29) 0.0;
  set vertexArray (ii + 30) fsubx;
  set vertexArray (ii + 31) fsuby;
  let jj = (!env).batch.elementPtr;
  let elementArray = (!env).batch.elementArray;
  set elementArray jj (ii / vertexSize);
  set elementArray (jj + 1) (ii / vertexSize + 1);
  set elementArray (jj + 2) (ii / vertexSize + 2);
  set elementArray (jj + 3) (ii / vertexSize + 1);
  set elementArray (jj + 4) (ii / vertexSize + 2);
  set elementArray (jj + 5) (ii / vertexSize + 3);
  (!env).batch.vertexPtr = ii + 4 * vertexSize;
  (!env).batch.elementPtr = jj + 6;
  (!env).batch.currTex = Some textureBuffer
};


/** Recomputes matrices while resetting size of window */
let resetSize env width height => {
  env := {...!env, size: {...(!env).size, width, height}};
  Gl.viewport context::(!env).gl x::0 y::0 ::width ::height;
  Gl.clearColor context::(!env).gl r::0. g::0. b::0. a::1.;
  Gl.Mat4.ortho
    out::(!env).camera.projectionMatrix
    left::0.
    right::(float_of_int width)
    bottom::(float_of_int height)
    top::0.
    near::0.
    far::1.;

  /** Tell OpenGL about what the uniform called `pMatrixUniform` is, here it's the projectionMatrix. **/
  Gl.uniformMatrix4fv
    context::(!env).gl location::(!env).pMatrixUniform value::(!env).camera.projectionMatrix
};
