// Small game using the same Wine Mono XNA-to-FNA path as Celeste.
using System;
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

sealed class FnaWitness : Game
{
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    static extern void OutputDebugStringA(string text);
    static void Emit(string text) { OutputDebugStringA("fna-runtime: " + text + "\n"); }
    readonly GraphicsDeviceManager graphics;
    SpriteBatch sprites;
    Texture2D pixel;
    int frames, x = 40;
    bool rightHeld;
    DateTime started;

    FnaWitness()
    {
        graphics = new GraphicsDeviceManager(this);
        graphics.PreferredBackBufferWidth = 640;
        graphics.PreferredBackBufferHeight = 480;
        IsMouseVisible = true;
        Window.Title = "FNA rendering and input witness";
    }
    protected override void LoadContent()
    {
        sprites = new SpriteBatch(GraphicsDevice);
        pixel = new Texture2D(GraphicsDevice, 1, 1);
        pixel.SetData(new[] { Color.White });
        var read = new Color[1]; pixel.GetData(read);
        if (read[0] != Color.White) throw new Exception("Texture upload/readback mismatch");
        started = DateTime.UtcNow;
        Emit("LOADED TEXTURE_ROUNDTRIP");
    }
    protected override void Update(GameTime time)
    {
        bool right = Keyboard.GetState().IsKeyDown(Keys.Right);
        if (right && !rightHeld) { x += 120; Emit("RIGHT x=" + x); }
        rightHeld = right;
        if (DateTime.UtcNow - started > TimeSpan.FromSeconds(120)) Exit();
        base.Update(time);
    }
    protected override void Draw(GameTime time)
    {
        GraphicsDevice.Clear(new Color(25, 55, 100));
        sprites.Begin();
        sprites.Draw(pixel, new Rectangle(x, 40, 128, 128), Color.Red);
        sprites.End();
        // Subsequent frames prove the Game loop survived its initial present.
        if (++frames == 5) Emit("READY");
        base.Draw(time);
    }
    static int Main()
    {
        Environment.SetEnvironmentVariable("FNA3D_FORCE_DRIVER", "OpenGL");
        Emit("BEGIN");
        try { using (var game = new FnaWitness()) game.Run(); return 0; }
        catch (Exception error) { Emit("FAIL " + error); return 1; }
    }
}
