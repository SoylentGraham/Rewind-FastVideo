using UnityEngine;
using System.Collections;
using System.Runtime.InteropServices;



enum UnityEvent
{
    OnPostRender = 0,
};

enum FastVideoError
{
	UknownError			= 0,
	DecoderError		= 1,
	CodecError			= 2,
	FileNotFound		= 3,
}

//	class that interfaces with FastVideo
public class FastVideo : MonoBehaviour
{
	[DllImport ("FastVideo")]	private static extern ulong	AllocInstance();
	[DllImport ("FastVideo")]	private static extern bool	FreeInstance(ulong Instance);
	[DllImport ("FastVideo")]	private static extern void	SetDebugLogFunction(System.IntPtr FunctionPtr);
	[DllImport ("FastVideo")]	private static extern void	SetOnErrorFunction(System.IntPtr FunctionPtr);
	[DllImport ("FastVideo")]	private static extern bool	SetTexture(ulong Instance,System.IntPtr Texture);
	[DllImport ("FastVideo")]	private static extern bool	SetVideo(ulong Instance,char[] Filename, int Length);
	[DllImport ("FastVideo")]	private static extern bool	Pause(ulong Instance);
	[DllImport ("FastVideo")]	private static extern bool	Resume(ulong Instance);
	[DllImport ("FastVideo")]	private static extern bool	SetLooping(ulong Instance,bool EnableLooping);
	[DllImport ("FastVideo")]	public static extern void	EnableTestDecoder(bool Enable);
	[DllImport ("FastVideo")]	public static extern void	EnableDebugTimers(bool Enable);
	[DllImport ("FastVideo")]	public static extern void	EnableDebugLag(bool Enable);
	[DllImport ("FastVideo")]	public static extern void	EnableDebugError(bool Enable);
	[DllImport ("FastVideo")]	public static extern void	EnableDebugFull(bool Enable);

	//	delegate type and singleton
	static private bool 			gCallbacksInitialised = false;
	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	private delegate void			DebugLogDelegate(string str);
	static private DebugLogDelegate	gDebugLogDelegate;

	[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
	private delegate void			OnErrorDelegate(ulong InstanceId,ulong ErrorId);
	static private OnErrorDelegate	gOnErrorDelegate;
	
	static void	InitCallbacks()
	{
		if ( gCallbacksInitialised )
			return;

		{
			gDebugLogDelegate = new DebugLogDelegate( DebugLogCallBack );
			//	setup debug callback
			// Convert callback_delegate into a function pointer that can be used in unmanaged code.
			System.IntPtr intptr_delegate = Marshal.GetFunctionPointerForDelegate( gDebugLogDelegate );
			SetDebugLogFunction( intptr_delegate );
		}

		{
			gOnErrorDelegate = new OnErrorDelegate( OnErrorCallBack );
			//	setup debug callback
			// Convert callback_delegate into a function pointer that can be used in unmanaged code.
			System.IntPtr intptr_delegate = Marshal.GetFunctionPointerForDelegate( gOnErrorDelegate );
			SetOnErrorFunction( intptr_delegate );
		}

		gCallbacksInitialised = true;
	}
	
	static void DebugLogCallBack(string str)
	{
	    Debug.Log("FastVideo: " + str);
	}
	
	static void OnErrorCallBack(ulong InstanceId,ulong ErrorId)
	{
	    Debug.Log("ERROR CALLBACK: " + ErrorId);
	}
	


	//	real class
	private ulong			mInstance = 0;
    	
	public FastVideo()
	{
		InitCallbacks();

		//	alloc plugin side instanace
		mInstance = AllocInstance();
		//SetMaxFrameBuffer( 20 );
	}
	
	~FastVideo()
	{
		FreeInstance( mInstance );
	}
	
	public void SetTexture(Texture2D Texture)
	{
		SetTexture( mInstance, Texture.GetNativeTexturePtr() );
	}			
	
	public void SetVideo(string Filename)
	{
		SetVideo( mInstance, Filename.ToCharArray(), Filename.Length );
	}

    public void Pause()
    {
        Pause(mInstance);
    }

    public void Resume()
    {
        Resume(mInstance);
    }

    public void SetLooping(bool EnableLooping)
    {
        SetLooping(mInstance,EnableLooping);
    }

    //	create end-of-render thread callback
	IEnumerator Start() 
	{
		
		//	Create a texture to render to
		Texture2D tex = new Texture2D(4096,2048,TextureFormat.ARGB32,false);
		// Set point filtering just so we can see the pixels clearly
		tex.filterMode = FilterMode.Point;
		// Call Apply() so it's actually uploaded to the GPU
		tex.Apply();

		// Set texture onto our matrial
		renderer.material.mainTexture = tex;
		
		SetTexture( tex );
		
		//	start decoding to texture
		//System.String Filename = "C:\\Users\\RWD Artist\\Desktop\\RWD_Lexus_4k_recut.ogv";
		//System.String Filename = "D:\\PopTrack\\data\\hands___\\hands1__.mov";
        //System.String Filename = "D:\\bike 4096-2048.mov";
        //System.String Filename = "D:\\bike 4000-2000.mov";
		//SetVideo( Filename );
		
		
		
		
		
		
		
		//	start thread
		yield return StartCoroutine("OnPostRenderThread");
	}
	
	void Reset() 
	{
		//	stop thread
		StopCoroutine("OnPostRenderThread");
	}
	
	private IEnumerator OnPostRenderThread()
	{
		while (true) 
		{
			// Wait until all frame rendering is done
			yield return new WaitForEndOfFrame();
			
			// Set time for the plugin
			//SetTimeFromUnity (Time.timeSinceLevelLoad);
			
			// Issue a plugin event with arbitrary integer identifier.
			// The plugin can distinguish between different
			// things it needs to do based on this ID.
			// For our simple plugin, it does not matter which ID we pass here.
            GL.IssuePluginEvent( (int)UnityEvent.OnPostRender );
		}
	}
}



public class UseRenderingPlugin : FastVideo {
	
	/*
	void Start() 
	{
		//	Create a texture to render to
		Texture2D tex = new Texture2D(4096,2048,TextureFormat.ARGB32,false);
		// Set point filtering just so we can see the pixels clearly
		tex.filterMode = FilterMode.Point;
		// Call Apply() so it's actually uploaded to the GPU
		tex.Apply();

		// Set texture onto our matrial
		renderer.material.mainTexture = tex;
		
		SetTexture( tex );
		
		//	start decoding to texture
		//System.String Filename = "C:\\Users\\RWD Artist\\Desktop\\RWD_Lexus_4k_recut.ogv";
		System.String Filename = "D:\\PopTrack\\data\\hands___\\hands1__.mov";
		SetVideo( Filename );
	}
	*/

    void OnMouseOver()
	{
        if (Input.GetMouseButton(0))
        {
            //	change/restart video
           	System.String Filename;
           // Filename = "D:\\bike 4096-2048.mov";
            // Filename = "D:\\bike-x264.mpg";
             Filename = "D:\\bike-x264.mpg";
           // Filename = "D:\\bike rgba.mpg";
           	Filename = "g:\\bike.h264";
            Filename = "rtsp://video.frostburg.edu:1935/ccit/ccit2.stream";
            //Filename = "d:\\iel_sgi.mpg";
			//Filename = "fdhsjfhdjkghfdk";

 		//EnableDebugTimers( true );
		//EnableDebugLag( true );
		//	EnableTestDecoder(false);
		SetVideo(Filename);
        }
        if (Input.GetMouseButton(1))
        {
            Debug.Log("PAUSING");
            Pause();
        }
        if (Input.GetMouseButton(2))
        {
            Debug.Log("RESUMING");
            Resume();
        }
    }
}
