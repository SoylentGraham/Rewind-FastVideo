using UnityEngine;
using System.Collections;
using System.Runtime.InteropServices;

public class UseRenderingPlugin : MonoBehaviour {
	
	//	gr: set a debug-log function to print to (which we then show in the console)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void MyDelegate(string str);
	[DllImport ("FastVideo")]
	private static extern void SetDebugLogFunction(System.IntPtr FunctionPtr);
	static void DebugLogCallBack(string str)
	{
	    Debug.Log("FastVideo: " + str);
	}

	//	functions in plugin...
	
	//	set current time for buffering/throw away buffered frames etc and correct timing
	[DllImport ("FastVideo")]
	private static extern void SetTimeFromUnity(float t);

	//	gr: note: char in c# seems to be a wchar
	[DllImport ("FastVideo")]
	private static extern void SetTextureFromUnity(System.IntPtr texture, char[] Filename, int Length);

	[DllImport ("FastVideo")]
	private static extern void SetMaxFrameBuffer(int MaxFrameBuffer);
	
	[DllImport ("FastVideo")]
	private static extern void SetMaxFramePool(int MaxFramePool);


	IEnumerator Start () 
	{
		// Convert callback_delegate into a function pointer that can be used in unmanaged code.
		System.IntPtr intptr_delegate = Marshal.GetFunctionPointerForDelegate( new MyDelegate( DebugLogCallBack ) );
		SetDebugLogFunction( intptr_delegate );
		
		
		CreateTextureAndPassToPlugin();
		
		//	start thread
		yield return StartCoroutine("CallPluginAtEndOfFrames");
	}

	private void CreateTextureAndPassToPlugin()
	{
		//	Create a texture to render to
		Texture2D tex = new Texture2D(4096,2048,TextureFormat.ARGB32,false);
		// Set point filtering just so we can see the pixels clearly
		tex.filterMode = FilterMode.Point;
		// Call Apply() so it's actually uploaded to the GPU
		tex.Apply();

		// Set texture onto our matrial
		renderer.material.mainTexture = tex;

		//	start decoding to texture
		System.String Filename = "C:\\Users\\RWD Artist\\Desktop\\RWD_Lexus_4k_recut.ogv";
		SetTextureFromUnity(tex.GetNativeTexturePtr(), Filename.ToCharArray(), Filename.Length );
		SetMaxFrameBuffer( 20 );
	}
	
	private IEnumerator CallPluginAtEndOfFrames ()
	{
		while (true) {
			// Wait until all frame rendering is done
			yield return new WaitForEndOfFrame();
			
			// Set time for the plugin
			SetTimeFromUnity (Time.timeSinceLevelLoad);
			
			// Issue a plugin event with arbitrary integer identifier.
			// The plugin can distinguish between different
			// things it needs to do based on this ID.
			// For our simple plugin, it does not matter which ID we pass here.
			GL.IssuePluginEvent (1);
		}
	}
}
