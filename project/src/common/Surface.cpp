#include <Graphics.h>
#include <Surface.h>
#include <nme/Pixel.h>

namespace nme
{

int gTextureContextVersion = 1;


// --- Surface -------------------------------------------------------


Surface::~Surface()
{
   delete mTexture;
}

void Surface::Bind(HardwareContext &inHardware,int inSlot)
{
   if (mTexture && !mTexture->IsCurrentVersion())
   {
      delete mTexture;
      mTexture = 0;
   }
 
   if (!mTexture)
      mTexture = inHardware.CreateTexture(this,mFlags);

   mTexture->Bind(inSlot);
}

Texture *Surface::GetTexture(HardwareContext *inHardware,int inPlane)
{
   if (mTexture && !mTexture->IsCurrentVersion())
   {
      delete mTexture;
      mTexture = 0;
   }
   if (!mTexture && inHardware)
      mTexture = inHardware->CreateTexture(this,mFlags);
   return mTexture;
}




// --- SimpleSurface -------------------------------------------------------

SimpleSurface::SimpleSurface(int inWidth,int inHeight,PixelFormat inPixelFormat,int inByteAlign,PixelFormat inGPUFormat)
{
   mWidth = inWidth;
   mHeight = inHeight;
   mTexture = 0;
   mPixelFormat = inPixelFormat;
   mGPUPixelFormat = inPixelFormat;
 
   int pix_size = BytesPerPixel(inPixelFormat);

   if (inGPUFormat==pfNone)
   {
      if (inByteAlign>1)
      {
         mStride = inWidth * pix_size + inByteAlign -1;
         mStride -= mStride % inByteAlign;
      }
      else
      {
         mStride = inWidth*pix_size;
      }

      mBase = new unsigned char[mStride * mHeight+1];
      mBase[mStride*mHeight] = 69;
   }
   else
   {
      mStride = 0;
      mBase = 0;
      if (inGPUFormat!=0)
         mGPUPixelFormat = inGPUFormat;

      createHardwareSurface();
   }
}

SimpleSurface::~SimpleSurface()
{
   if (mBase)
   {
      if (mBase[mStride*mHeight]!=69)
      {
         ELOG("Image write overflow");
         *(int *)0=0;
      }
      delete [] mBase;
   }
}


void SimpleSurface::destroyHardwareSurface() {

  if (mTexture )
   {
      delete mTexture;
      mTexture = 0;
   }
   
}


void SimpleSurface::createHardwareSurface() {

   if ( nme::HardwareRenderer::current == NULL )
      printf( "Null Hardware Context" );
   else
       GetTexture( nme::HardwareRenderer::current );
   
}

void SimpleSurface::MakeTextureOnly()
{ 
   if(mBase)
   {
       createHardwareSurface();
       delete [] mBase;
       mBase = NULL;
   }
}

void SimpleSurface::ChangeInternalFormat(PixelFormat inNewFormat, const Rect *inIgnore)
{
   if (!mBase)
      return;

   PixelFormat newFormat = inNewFormat;
   // Convert to render target type...
   if (newFormat==pfNone)
      switch(mPixelFormat)
      {
         case pfLuma:  newFormat = pfRGB; break;
         case pfLumaAlpha:  newFormat = pfBGRA; break;
         case pfRGB32f:  newFormat = pfRGB; break;
         case pfRGBA32f:  newFormat = pfBGRA; break;
         case pfRGBA:  newFormat = pfBGRA; break;
         case pfRGBPremA:  newFormat = pfBGRPremA; break;
         case pfRGB565:  newFormat = pfRGB; break;
         case pfARGB4444:  newFormat = pfBGRA; break;
         default:
           newFormat = pfRGB;
     }

   int newSize = BytesPerPixel(newFormat);
   int newStride = mWidth * newSize;
   unsigned char *newBuffer = mBase = new unsigned char[newStride * mHeight+1];
   newBuffer[newStride*mHeight] = 69;

   if (inIgnore==0)
   {
     PixelConvert(mWidth, mHeight,
       mPixelFormat,  mBase, mStride, GetPlaneOffset(),
       newFormat, newBuffer, newStride, 0 );
   }
   else
   {
      /*
          TTTTTTT
          L  X  R
          BBBBBBB
      */
      Rect r = *inIgnore;
      if (r.y>0)
      {
         PixelConvert(mWidth, r.y,
           mPixelFormat,  mBase, mStride, GetPlaneOffset(),
           newFormat, newBuffer, newStride, 0 );
      }
      if (r.x>0)
      {
         PixelConvert(r.x, r.h,
           mPixelFormat,  mBase + mStride*r.y, mStride, GetPlaneOffset(),
           newFormat, newBuffer + newStride*r.y, newStride, 0 );
      }
      if (r.x1()<mWidth)
      {
         int oldPw = BytesPerPixel(mPixelFormat);
         PixelConvert(mWidth-r.x1(), r.h,
           mPixelFormat,  mBase + mStride*r.y + r.x1()*oldPw, mStride, GetPlaneOffset(),
           newFormat, newBuffer + newStride*r.y + r.x1()*newSize, newStride, 0 );
      }

      if (r.y1()<mHeight)
      {
         PixelConvert(mWidth, mHeight-r.y1(),
           mPixelFormat,  mBase + mStride*r.y1(), mStride, GetPlaneOffset(),
           newFormat, newBuffer + newStride*r.y1(), newStride, 0 );
      }
   }
   delete [] mBase;
   mBase = newBuffer;
   mStride = newStride;
   mPixelFormat = newFormat;
   mGPUPixelFormat = mPixelFormat;
}



// --- Surface Blitting ------------------------------------------------------------------

struct NullMask
{
   inline void SetPos(int inX,int inY) const { }
   inline int MaskAlpha(int inAlpha) const { return inAlpha; }
   inline uint8 MaskAlpha(const ARGB &inRGB) const { return inRGB.a; }
   inline uint8 MaskAlpha(const BGRPremA &inRGB) const { return inRGB.a; }
   inline uint8 MaskAlpha(const RGB &inRGB) const { return 255; }
   template<typename T>
   T Mask(T inT) const { return inT; }
};


struct ImageMask
{
   ImageMask(const BitmapCache &inMask) :
      mMask(inMask), mOx(inMask.GetDestX()), mOy(inMask.GetDestY())
   {
      if (mMask.Format()==pfAlpha)
      {
         mComponentOffset = 0;
         mPixelStride = 1;
      }
      else
      {
         ARGB tmp;
         mComponentOffset = (uint8 *)&tmp.a - (uint8 *)&tmp;
         mPixelStride = 4;
      }
   }

   inline void SetPos(int inX,int inY) const
   {
      mRow = (mMask.Row(inY-mOy) + mComponentOffset) + mPixelStride*(inX-mOx);
   }

   inline uint8 MaskAlpha(uint8 inAlpha) const
   {
      inAlpha = (inAlpha * (*mRow) ) >> 8;
      mRow += mPixelStride;
      return inAlpha;
   }
   inline uint8 MaskAlpha(ARGB inARGB) const
   {
      int a = (inARGB.a * (*mRow) ) >> 8;
      mRow += mPixelStride;
      return a;
   }


   inline AlphaPixel Mask(const AlphaPixel &inA) const
   {
      AlphaPixel result;
      result.a = (inA.a * (*mRow + *mRow) )>>8;
      mRow += mPixelStride;
      return result;
   }
   inline BGRPremA Mask(const RGB &inRGB) const
   {
      BGRPremA result;
      Uint8 *lut = gPremAlphaLut[*mRow];
      result.r = lut[inRGB.r];
      result.g = lut[inRGB.g];
      result.b = lut[inRGB.b];
      result.a = *mRow;
      mRow += mPixelStride;
      return result;
   }

   template<bool PREM>
   inline BGRA<PREM> Mask(const BGRA<PREM> &inBgra) const
   {
      BGRA<PREM> result;
      if (PREM)
      {
         Uint8 *lut = gPremAlphaLut[*mRow];
         result.r = lut[inBgra.r];
         result.g = lut[inBgra.g];
         result.b = lut[inBgra.b];
         result.a = lut[inBgra.a];
      }
      else
      {
         result.ival = inBgra.ival;
         result.a = (inBgra.a * (*mRow) ) >> 8;
      }
      mRow += mPixelStride;
      return result;
   }




   const BitmapCache &mMask;
   mutable const uint8 *mRow;
   int mOx,mOy;
   int mComponentOffset;
   int mPixelStride;
};

template<typename PIXEL>
struct ImageSource
{
   typedef PIXEL Pixel;

   ImageSource(const uint8 *inBase, int inStride)
   {
      mBase = inBase;
      mStride = inStride;
   }

   inline void SetPos(int inX,int inY) const
   {
      mPos = ((const PIXEL *)( mBase + mStride*inY)) + inX;
   }
   inline const Pixel &Next() const { return *mPos++; }

   inline int getNextAlpha() const { return mPos++ -> a; }


   mutable const PIXEL *mPos;
   int   mStride;
   const uint8 *mBase;
};

struct FullAlpha
{
   inline void SetPos(int inX,int inY) const { }
   inline int getNextAlpha() const{ return 255; }
};



template<bool INNER,bool TINT_RGB=false>
struct TintSource
{
   typedef ARGB Pixel;

   TintSource(const uint8 *inBase, int inStride, int inCol,PixelFormat inFormat)
   {
      mBase = inBase;
      mStride = inStride;
      mCol = ARGB(inCol);
      a0 = mCol.a; if (a0>127) a0++;
      r = mCol.r; if (r>127) r++;
      g = mCol.g; if (g>127) g++;
      b = mCol.b; if (b>127) b++;
      mFormat = inFormat;

      if (inFormat==pfAlpha)
      {
         mComponentOffset = 0;
         mPixelStride = 1;
      }
      else
      {
         ARGB tmp;
         mComponentOffset = (uint8 *)&tmp.a - (uint8 *)&tmp;
         mPixelStride = 4;
      }
   }

   inline void SetPos(int inX,int inY) const
   {
      if (TINT_RGB)
         mPos = ((const uint8 *)( mBase + mStride*inY)) + inX*mPixelStride;
      else
         mPos = ((const uint8 *)( mBase + mStride*inY)) + inX*mPixelStride + mComponentOffset;
   }
   inline const ARGB &Next() const
   {
      if (INNER)
         mCol.a =  a0*(255 - *mPos)>>8;
      else if (TINT_RGB)
      {
         ARGB col = *(ARGB *)(mPos);
         mCol.a =   (a0*col.a)>>8;
         mCol.r =  (r*col.r)>>8;
         mCol.g =  (g*col.g)>>8;
         mCol.b =  (b*col.b)>>8;
      }
      else
      {
         mCol.a =  (a0 * *mPos)>>8;
      }
      mPos+=mPixelStride;
      return mCol;
   }

   int a0;
   int r;
   int g;
   int b;
   PixelFormat mFormat;
   mutable ARGB mCol;
   mutable const uint8 *mPos;
   int   mComponentOffset;
   int   mPixelStride;
   int   mStride;
   const uint8 *mBase;
};


template<typename PIXEL>
struct ImageDest
{
   typedef PIXEL Pixel;

   ImageDest(const RenderTarget &inTarget) : mTarget(inTarget) { }

   inline void SetPos(int inX,int inY) const
   {
      mPos = ((PIXEL *)mTarget.Row(inY)) + inX;
   }
   inline Pixel &Next() const { return *mPos++; }

   PixelFormat Format() const { return mTarget.mPixelFormat; }

   const RenderTarget &mTarget;
   mutable PIXEL *mPos;
};


template<typename DEST, typename SRC, typename MASK>
void TBlit( const DEST &outDest, const SRC &inSrc,const MASK &inMask,
            int inX, int inY, const Rect &inSrcRect)
{
   for(int y=0;y<inSrcRect.h;y++)
   {
      outDest.SetPos(inX , inY + y );
      inMask.SetPos(inX , inY + y );
      inSrc.SetPos( inSrcRect.x, inSrcRect.y + y );
      for(int x=0;x<inSrcRect.w;x++)
         BlendPixel(outDest.Next(),inMask.Mask(inSrc.Next()));
   }
}



template<typename DEST, typename SRC, typename MASK>
void TBlitAlpha( const DEST &outDest, const SRC &inSrc,const MASK &inMask,
            int inX, int inY, const Rect &inSrcRect)
{
   for(int y=0;y<inSrcRect.h;y++)
   {
      outDest.SetPos(inX + inSrcRect.x, inY + y+inSrcRect.y );
      inMask.SetPos(inX + inSrcRect.x, inY + y+inSrcRect.y );
      inSrc.SetPos( inSrcRect.x, inSrcRect.y + y );
      for(int x=0;x<inSrcRect.w;x++)
         BlendAlpha(outDest.Next(),inMask.MaskAlpha(inSrc.getNextAlpha()));
   }

}

static uint8 sgClamp0255Values[256*3];
static uint8 *sgClamp0255;
int InitClamp()
{
   sgClamp0255 = sgClamp0255Values + 256;
   for(int i=-255; i<=255+255;i++)
      sgClamp0255[i] = i<0 ? 0 : i>255 ? 255 : i;
   return 0;
}
static int init_clamp = InitClamp();

typedef void (*BlendFunc)(ARGB &ioDest, ARGB inSrc);

template<bool DEST_ALPHA,typename FUNC>
inline void BlendFuncWithAlpha(ARGB &ioDest, ARGB &inSrc,FUNC F)
{
   if (inSrc.a==0)
      return;
   ARGB val = inSrc;
   if (!DEST_ALPHA || ioDest.a>0)
   {
      F(val.r,ioDest.r);
      F(val.g,ioDest.g);
      F(val.b,ioDest.b);
   }
   if (DEST_ALPHA && ioDest.a<255)
   {
      int A = ioDest.a + (ioDest.a>>7);
      int A_ = 256-A;
      val.r = (val.r *A + inSrc.r*A_)>>8;
      val.g = (val.g *A + inSrc.g*A_)>>8;
      val.b = (val.b *A + inSrc.b*A_)>>8;
   }
   if (val.a==255)
   {
      ioDest = val;
      return;
   }

   if (DEST_ALPHA)
      ioDest.QBlendA(val);
   else
      ioDest.QBlend(val);
}


template<typename SRC, typename FUNC>
RGB ApplyComponent(const RGB &d, const SRC &s, const FUNC &)
{
   RGB result;
   result.r = FUNC::comp(d.r, s.getR() );
   result.g = FUNC::comp(d.g, s.getG() );
   result.b = FUNC::comp(d.b, s.getB() );
   return result;
}

template<typename SRC, typename FUNC>
ARGB ApplyComponent(const ARGB &d, const SRC &s, const FUNC &)
{
   ARGB result;
   result.r = FUNC::comp(d.r, s.getR() );
   result.g = FUNC::comp(d.g, s.getG() );
   result.b = FUNC::comp(d.b, s.getB() );
   result.a = FUNC::alpha(d.a, s.getAlpha() );
   return result;
}


template<typename SRC, typename FUNC>
BGRPremA ApplyComponent(const BGRPremA &d, const SRC &s, const FUNC &)
{
   BGRPremA result;
   result.r = FUNC::comp(d.r, s.getRAlpha() );
   result.g = FUNC::comp(d.g, s.getGAlpha() );
   result.b = FUNC::comp(d.b, s.getBAlpha() );
   result.a = FUNC::alpha(d.a, s.getAlpha() );
   return result;
}


template<typename SRC, typename FUNC>
AlphaPixel ApplyComponent(const AlphaPixel &d, const SRC &s, const FUNC &)
{
   AlphaPixel result;
   result.a = FUNC::alpha(d.a, s.getAlpha());
   return result;
}


// --- Multiply -----

struct MultiplyHandler
{
   static inline uint8 comp(uint8 a, uint8 b) { return ( (a + (a>>7)) * b ) >> 8; }
   static inline uint8 alpha(uint8 a, uint8 b) { return ( (a + (a>>7)) * b ) >> 8; }
};



// --- Screen -----

struct ScreenHandler
{
   static inline uint8 comp(uint8 a, uint8 b) { return 255 - (((255 - a) * ( 256 - b - (b>>7)))>>8); }
   static inline uint8 alpha(uint8 a, uint8 b) { return 255 - (((255 - a) * ( 256 - b - (b>>7)))>>8); }
};



template<bool DEST_ALPHA> void ScreenFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoScreen()); }

// --- Lighten -----

struct DoLighten
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { if (inDest > ioVal ) ioVal = inDest; }
};

template<bool DEST_ALPHA> void LightenFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoLighten()); }

// --- Darken -----

struct DoDarken
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { if (inDest < ioVal ) ioVal = inDest; }
};

template<bool DEST_ALPHA> void DarkenFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoDarken()); }

// --- Difference -----

struct DoDifference
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { if (inDest < ioVal ) ioVal -= inDest; else ioVal = inDest-ioVal; }
};

template<bool DEST_ALPHA> void DifferenceFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoDifference()); }

// --- Add -----

struct DoAdd
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { ioVal = sgClamp0255[ioVal+inDest]; }
};

template< bool DEST_ALPHA> void AddFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoAdd()); }

// --- Subtract -----

struct DoSubtract
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { ioVal = sgClamp0255[inDest-ioVal]; }
};

template<bool DEST_ALPHA> void SubtractFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoSubtract()); }

// --- Invert -----

struct DoInvert
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { ioVal = 255 - inDest; }
};

template< bool DEST_ALPHA> void InvertFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoInvert()); }

// --- Alpha -----

template<bool DEST_ALPHA> void AlphaFunc(ARGB &ioDest, ARGB inSrc)
{
   if (DEST_ALPHA)
      ioDest.a = (ioDest.a * ( inSrc.a + (inSrc.a>>7))) >> 8;
}

// --- Erase -----

template< bool DEST_ALPHA> void EraseFunc(ARGB &ioDest, ARGB inSrc)
{
   if (DEST_ALPHA)
      ioDest.a = (ioDest.a * ( 256-inSrc.a - (inSrc.a>>7))) >> 8;
}

// --- Overlay -----

/*
struct DoOverlay
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { if (inDest>127) DoScreen()(ioVal,inDest); else DoMult()(ioVal,inDest); }
};
*/

template<bool DEST_ALPHA> void OverlayFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoOverlay()); }

// --- HardLight -----

   /*
struct DoHardLight
{
   inline void operator()(uint8 &ioVal,uint8 inDest) const
   { if (ioVal>127) DoScreen()(ioVal,inDest); else DoMult()(ioVal,inDest); }
};
*/

template<bool DEST_ALPHA> void HardLightFunc(ARGB &ioDest, ARGB inSrc)
   { BlendFuncWithAlpha<DEST_ALPHA>(ioDest,inSrc,DoHardLight()); }

// -- Set ---------

template<bool DEST_ALPHA> void CopyFunc(ARGB &ioDest, ARGB inSrc)
{
   ioDest = inSrc;
}

struct CopyHandler
{
   static inline uint8 comp(uint8 a, uint8 b) { return b; }
   static inline uint8 alpha(uint8 a, uint8 b) { return a; }
};

// -- Inner ---------

template<bool DEST_ALPHA> void InnerFunc(ARGB &ioDest, ARGB inSrc)
{
   int A = inSrc.a;
   if (A)
   {
      ioDest.r += ((inSrc.r - ioDest.r)*A)>>8;
      ioDest.g += ((inSrc.g - ioDest.g)*A)>>8;
      ioDest.b += ((inSrc.b - ioDest.b)*A)>>8;
   }
}

template<typename DEST, typename SOURCE, typename MASK>
void TBlitBlend( const DEST &outDest, SOURCE &inSrc,const MASK &inMask,
            int inX, int inY, const Rect &inSrcRect, BlendMode inMode)
{
   for(int y=0;y<inSrcRect.h;y++)
   {
      outDest.SetPos(inX , inY + y );
      inMask.SetPos(inX , inY + y );
      inSrc.SetPos( inSrcRect.x, inSrcRect.y + y );

      #define BLEND_CASE(mode) \
         case bm##mode: \
            for(int x=0;x<inSrcRect.w;x++) \
            { \
               DEST::Pixel &dest = outDest.Next(); \
               BlendPixel(dest,ApplyComponent(dest,inMask.Mask(inSrc.Next()),mode##Handler() ) ); \
            } \
            break;

      switch(inMode)
      {
         BLEND_CASE(Multiply)
         BLEND_CASE(Screen)
         BLEND_CASE(Copy)
         /*
         BLEND_CASE(Lighten)
         BLEND_CASE(Darken)
         BLEND_CASE(Difference)
         BLEND_CASE(Add)
         BLEND_CASE(Subtract)
         BLEND_CASE(Invert)
         BLEND_CASE(Alpha)
         BLEND_CASE(Erase)
         BLEND_CASE(Overlay)
         BLEND_CASE(HardLight)
         BLEND_CASE(Inner)
         */
      }
   }
}


template<typename DEST,typename SRC>
void TTBlitRgb(DEST &dest, SRC &src, int dx, int dy, Rect src_rect, const BitmapCache *inMask, BlendMode inBlend )
{
   if (inBlend==bmNormal || inBlend==bmLayer)
   {
      if (inMask)
         TBlit( dest, src, ImageMask(*inMask), dx, dy, src_rect );
      else
         TBlit( dest, src, NullMask(), dx, dy, src_rect );
   }
   else
   {
      if (inMask)
         TBlitBlend( dest, src, ImageMask(*inMask), dx, dy, src_rect, inBlend );
      else
         TBlitBlend( dest, src, NullMask(), dx, dy, src_rect, inBlend );
   }
}




template<typename DEST>
void TBlitRgb(DEST &dest, int dx, int dy, const SimpleSurface *src, Rect src_rect, const BitmapCache *inMask, BlendMode inBlend, uint32 inTint )
{
      bool tint = inBlend==bmTinted;
      bool tint_inner = inBlend==bmTintedInner;
      bool tint_add = inBlend==bmTintedAdd;

      bool src_alpha = src->Format()==pfAlpha;

      // Blitting tint, we can ignore blend mode too (this is used for rendering text)
      if (tint)
      {
         if (src_alpha)
         {
            TintSource<false> src(src->GetBase(),src->GetStride(),inTint,src->Format());
            if (inMask)
               TBlit( dest, src, ImageMask(*inMask), dx, dy, src_rect );
            else
               TBlit( dest, src, NullMask(), dx, dy, src_rect );
         }
         else
         {
            TintSource<false,true> src(src->GetBase(),src->GetStride(),inTint,src->Format());
            if (inMask)
               TBlit( dest, src, ImageMask(*inMask), dx, dy, src_rect );
            else
               TBlit( dest, src, NullMask(), dx, dy, src_rect );
         }
      }
      else if (tint_inner)
      {
         TintSource<true> src(src->GetBase(),src->GetStride(),inTint,src->Format());

         if (inMask)
            TBlitBlend( dest, src, ImageMask(*inMask), dx, dy, src_rect, bmInner );
         else
            TBlitBlend( dest, src, NullMask(), dx, dy, src_rect, bmInner );
      }
      else if (tint_add)
      {
         TintSource<false,true> src(src->GetBase(),src->GetStride(),inTint,src->Format());

         if (inMask)
            TBlitBlend( dest, src, ImageMask(*inMask), dx, dy, src_rect, bmAdd );
         else
            TBlitBlend( dest, src, NullMask(), dx, dy, src_rect, bmAdd );
      }
      else
      {
         switch(src->Format())
         {
            case pfAlpha:
               TTBlitRgb(dest, ImageSource<AlphaPixel>(src->GetBase(),src->GetStride()), dx, dy, src_rect,inMask,inBlend);
               return;
            case pfRGB:
               TTBlitRgb(dest, ImageSource<RGB>(src->GetBase(),src->GetStride()), dx, dy, src_rect,inMask,inBlend);
               return;
            case pfBGRA:
               TTBlitRgb(dest, ImageSource<ARGB>(src->GetBase(),src->GetStride()), dx, dy, src_rect,inMask,inBlend);
               return;
            case pfBGRPremA:
               TTBlitRgb(dest, ImageSource<BGRPremA>(src->GetBase(),src->GetStride()), dx, dy, src_rect,inMask,inBlend);
               return;
            default:
               ;
         }
      }
}


void SimpleSurface::BlitTo(const RenderTarget &outDest,
                     const Rect &inSrcRect,int inPosX, int inPosY,
                     BlendMode inBlend, const BitmapCache *inMask,
                     uint32 inTint ) const
{
   if (!mBase)
      return;

   // Translate inSrcRect src_rect to dest ...
   Rect src_rect(inPosX,inPosY, inSrcRect.w, inSrcRect.h );
   // clip ...
   src_rect = src_rect.Intersect(outDest.mRect);

   if (inMask)
      src_rect = src_rect.Intersect(inMask->GetRect());

   // translate back to source-coordinates ...
   src_rect.Translate(inSrcRect.x-inPosX, inSrcRect.y-inPosY);
   // clip to origial rect...
   src_rect = src_rect.Intersect( inSrcRect );

   if (src_rect.HasPixels())
   {
      if (mPixelFormat>=pfRenderToCount)
         const_cast<SimpleSurface *>(this)->ChangeInternalFormat();


      bool src_alpha = mPixelFormat==pfAlpha;
      bool dest_alpha = outDest.mPixelFormat==pfAlpha;

      int dx = inPosX + src_rect.x - inSrcRect.x;
      int dy = inPosY + src_rect.y - inSrcRect.y;

      // Check for rendering same-surface to same-surface
      if (mPixelFormat == outDest.mPixelFormat)
      {
          int pw = BytesPerPixel(mPixelFormat);
          // If these are the same surface, then difference in pointers will be small enough,
          //  otherwise x_off and y_off could be greatly different
          int d_base = (outDest.mSoftPtr-mBase);
          int y_off = d_base/mStride;
          int x_off = (d_base-y_off*mStride)/pw;
          Rect dr(dx + x_off, dy + y_off, src_rect.w, src_rect.h);
          if (src_rect.Intersect(dr).HasPixels())
          {
              SimpleSurface sub(src_rect.w, src_rect.h, mPixelFormat);
              Rect sub_dest(0,0,src_rect.w, src_rect.h);

              for(int y=0;y<src_rect.h;y++)
                 memcpy((void *)sub.Row(y), Row(src_rect.y+y) + (src_rect.x*pw), src_rect.w*pw );

              sub.BlitTo(outDest, sub_dest, dx, dy, inBlend, 0, inTint);
              return;
          }
      }


      // Blitting to alpha image - can ignore blend mode
      if (dest_alpha)
      {
         ImageDest<AlphaPixel> dest(outDest);
         if (inMask)
         {
            if (src_alpha)
               TBlitAlpha(dest, ImageSource<AlphaPixel>(mBase,mStride), ImageMask(*inMask), dx, dy, src_rect );
            else if (mPixelFormat==pfBGRA || mPixelFormat==pfRGBPremA)
               TBlitAlpha(dest, ImageSource<ARGB>(mBase,mStride), ImageMask(*inMask), dx, dy, src_rect );
            else
               TBlitAlpha(dest, FullAlpha(), ImageMask(*inMask), dx, dy, src_rect );
         }
         else
         {
            if (src_alpha)
               TBlitAlpha(dest, ImageSource<AlphaPixel>(mBase,mStride), NullMask(), dx, dy, src_rect );
            else if (mPixelFormat==pfBGRA || mPixelFormat==pfRGBPremA)
               TBlitAlpha(dest, ImageSource<ARGB>(mBase,mStride), NullMask(), dx, dy, src_rect );
            else
               TBlitAlpha(dest, FullAlpha(), NullMask(), dx, dy, src_rect );
         }
      }
      else if (outDest.Format()==pfBGRPremA)
         TBlitRgb( ImageDest<BGRPremA>(outDest), dx, dy, this, src_rect, inMask, inBlend, inTint );
      else if (outDest.Format()==pfBGRA)
         TBlitRgb( ImageDest<ARGB>(outDest), dx, dy, this, src_rect, inMask, inBlend, inTint );
      else if (outDest.Format()==pfRGB)
         TBlitRgb( ImageDest<RGB>(outDest), dx, dy, this, src_rect, inMask, inBlend, inTint );
   }
}

void SimpleSurface::colorTransform(const Rect &inRect, ColorTransform &inTransform)
{
   if (mPixelFormat==pfAlpha || !mBase)
      return;

   const uint8 *ta = inTransform.GetAlphaLUT();
   const uint8 *tr = inTransform.GetRLUT();
   const uint8 *tg = inTransform.GetGLUT();
   const uint8 *tb = inTransform.GetBLUT();

   RenderTarget target = BeginRender(inRect,false);

   Rect r = target.mRect;
   for(int y=0;y<r.h;y++)
   {
      ARGB *rgb = ((ARGB *)target.Row(y+r.y)) + r.x;
      for(int x=0;x<r.w;x++)
      {
         rgb->r = tr[rgb->r];
         rgb->g = tg[rgb->g];
         rgb->b = tb[rgb->b];
         rgb->a = ta[rgb->a];
         rgb++;
      }
   }

   EndRender();
}




void SimpleSurface::BlitChannel(const RenderTarget &outTarget, const Rect &inSrcRect,
                   int inPosX, int inPosY,
                   int inSrcChannel, int inDestChannel ) const
{
   PixelFormat destFmt = outTarget.mPixelFormat;
   int destPos = GetPixelChannelOffset(destFmt,(PixelChannel)inDestChannel);
   if (destPos<0)
      return;

   PixelFormat srcFmt = mPixelFormat;
   int srcPos =GetPixelChannelOffset(srcFmt,(PixelChannel)inSrcChannel);
   if (srcPos==CHANNEL_OFFSET_NONE)
      return;

   int srcPw = BytesPerPixel(srcFmt);
   int destPw = BytesPerPixel(destFmt);


   bool set_255 = srcPos==CHANNEL_OFFSET_VIRTUAL_ALPHA;

   Rect src_rect(inSrcRect.x,inSrcRect.y, inSrcRect.w, inSrcRect.h );
   src_rect = src_rect.Intersect( Rect(0,0,Width(),Height() ) );

   Rect dest_rect(inPosX,inPosY, inSrcRect.w, inSrcRect.h );
   dest_rect = dest_rect.Intersect(outTarget.mRect);


   int minW = src_rect.w;
   if(dest_rect.w < src_rect.w)
      minW = dest_rect.w;

   int minH = src_rect.h;
   if(dest_rect.h < src_rect.h)
      minH = dest_rect.h;

   for(int y=0;y<minH;y++)
   {
      uint8 *d = outTarget.Row(y+dest_rect.y) + dest_rect.x*destPw + destPos;
      if (set_255)
      {
         for(int x=0;x<minW;x++)
         {
            *d = 255;
            d+=destPw;
         }
      }
      else
      {
         const uint8 *s = Row(y+src_rect.y) + src_rect.x * srcPw + srcPos;

         for(int x=0;x<minW;x++)
         {
            *d = *s;
            d+=destPw;
            s+=srcPw;
         }
      }
   }
}


template<typename SRC,typename DEST>
void TStretchTo(const SimpleSurface *inSrc,const RenderTarget &outTarget,
                const Rect &inSrcRect, const DRect &inDestRect)
{
   Rect irect( inDestRect.x+0.5, inDestRect.y+0.5, inDestRect.x1()+0.5, inDestRect.y1()+0.5, true);
   Rect out = irect.Intersect(outTarget.mRect);
   if (!out.Area())
      return;

   int dsx_dx = (inSrcRect.w << 16)/inDestRect.w;
   int dsy_dy = (inSrcRect.h << 16)/inDestRect.h;

   #ifndef STRETCH_BILINEAR
   // (Dx - inDestRect.x) * dsx_dx = ( Sx- inSrcRect.x )
   // Start first sample at out.x+0.5, and subtract 0.5 so src(1) is between first and second pixel
   //
   // Sx = (out.x+0.5-inDestRect.x)*dsx_dx + inSrcRect.x - 0.5

   //int sx0 = (int)((out.x-inDestRect.x*inSrcRect.w/inDestRect.w)*65536) +(inSrcRect.x<<16);
   //int sy0 = (int)((out.y-inDestRect.y*inSrcRect.h/inDestRect.h)*65536) +(inSrcRect.y<<16);
   int sx0 = (int)((out.x+0.5-inDestRect.x)*dsx_dx + (inSrcRect.x<<16) );
   int sy0 = (int)((out.y+0.5-inDestRect.y)*dsy_dy + (inSrcRect.y<<16) );

   for(int y=0;y<out.h;y++)
   {
      DEST *dest= (DEST *)outTarget.Row(y+out.y) + out.x;
      int y_ = (sy0>>16);
      const SRC *src = (const SRC *)inSrc->Row(y_);
      sy0+=dsy_dy;

      int sx = sx0;
      for(int x=0;x<out.w;x++)
         BlendPixel(*dest++, src[sx>>16]);
   }

   #else
   // todo - overflow testing
   // (Dx - inDestRect.x) * dsx_dx = ( Sx- inSrcRect.x )
   // Start first sample at out.x+0.5, and subtract 0.5 so src(1) is between first and second pixel
   //
   // Sx = (out.x+0.5-inDestRect.x)*dsx_dx + inSrcRect.x - 0.5
   int sx0 = (((((out.x-inDestRect.x)<<8) + 0x80) * inSrcRect.w/inDestRect.w) << 8) +(inSrcRect.x<<16) - 0x8000;
   int sy0 = (((((out.y-inDestRect.y)<<8) + 0x80) * inSrcRect.h/inDestRect.h) << 8) +(inSrcRect.y<<16) - 0x8000;
   int last_y = inSrcRect.y1()-1;
   SRC s;
   for(int y=0;y<out.h;y++)
   {
      DEST *dest= (DEST *)outTarget.Row(y+out.y) + out.x;
      int y_ = (sy0>>16);
      int y_frac = sy0 & 0xffff;
      const SRC *src0 = (const SRC *)inSrc->Row(y_);
      const SRC *src1 = (const SRC *)inSrc->Row(y_<last_y ? y_+1 : y_);
      sy0+=dsy_dy;

      int sx = sx0;
      for(int x=0;x<out.w;x++)
      {
         int x_ = sx>>16;
         int x_frac = sx & 0xffff;

         SRC s = BilinearInterp( src0[x_], src0[x_+1], src1[x_], src1[x_+1], x_frac, y_frac);

         BlendPixel(*dest, s);
         dest++;
         sx+=dsx_dx;
      }
   }
   #endif
}


template<typename PIXEL>
void TStretchSuraceTo(const SimpleSurface *inSurface, const RenderTarget &outTarget,
                     const Rect &inSrcRect, const DRect &inDestRect)
{
   switch(outTarget.Format())
   {
      case pfRGB:
         TStretchTo<PIXEL,RGB>(inSurface, outTarget, inSrcRect, inDestRect);
         break;
      case pfBGRA:
         TStretchTo<PIXEL,ARGB>(inSurface, outTarget, inSrcRect, inDestRect);
         break;
      case pfBGRPremA:
         TStretchTo<PIXEL,BGRPremA>(inSurface, outTarget, inSrcRect, inDestRect);
         break;
      case pfAlpha:
         TStretchTo<PIXEL,RGB>(inSurface, outTarget, inSrcRect, inDestRect);
         break;
   }
}

void SimpleSurface::StretchTo(const RenderTarget &outTarget,
                     const Rect &inSrcRect, const DRect &inDestRect) const
{
   switch(mPixelFormat)
   {
      case pfRGB:
         TStretchSuraceTo<RGB>(this, outTarget, inSrcRect, inDestRect);
         break;
      case pfBGRA:
         TStretchSuraceTo<ARGB>(this, outTarget, inSrcRect, inDestRect);
         break;
      case pfBGRPremA:
         TStretchSuraceTo<BGRPremA>(this, outTarget, inSrcRect, inDestRect);
         break;
      case pfAlpha:
         TStretchSuraceTo<RGB>(this, outTarget, inSrcRect, inDestRect);
         break;
   }
}



void SimpleSurface::Clear(uint32 inColour,const Rect *inRect)
{
   if (!mBase)
      return;
   if (mPixelFormat==pfLuma)
   {
      memset(mBase, inColour & 0xff,mStride*mHeight);
      return;
   }

   ARGB rgb(inColour);
   if (mPixelFormat==pfAlpha)
   {
      memset(mBase, rgb.a,mStride*mHeight);
      return;
   }

   int x0 = inRect ? inRect->x  : 0;
   int x1 = inRect ? inRect->x1()  : Width();
   int y0 = inRect ? inRect->y  : 0;
   int y1 = inRect ? inRect->y1()  : Height();
   if( x0 < 0 ) x0 = 0;
   if( x1 > Width() ) x1 = Width();
   if( y0 < 0 ) y0 = 0;
   if( y1 > Height() ) y1 = Height();
   if (x1<=x0 || y1<=y0)
      return;

   int pix_size = BytesPerPixel(mPixelFormat);

   if (mPixelFormat==pfLumaAlpha)
   {
      for(int y=y0;y<y1;y++)
      {
         int luma = rgb.luma();
         uint8 *ptr = (mBase + y*mStride) + x0*2;
         for(int x=x0;x<x1;x++)
         {
            *ptr++ = luma;
            *ptr++ = rgb.a;
         }
      }

   }
   else if (mPixelFormat==pfRGB)
   {
      for(int y=y0;y<y1;y++)
      {
         uint8 *ptr = (mBase + y*mStride) + x0*3;
         for(int x=x0;x<x1;x++)
         {
            *ptr++ = rgb.r;
            *ptr++ = rgb.g;
            *ptr++ = rgb.b;
         }
      }
   }
   else if (pix_size==4)
   {
      if (mPixelFormat==pfBGRPremA)
      {
         BGRPremA prem;
         SetPixel(prem,rgb);
         rgb.ival = prem.ival;
      }
      for(int y=y0;y<y1;y++)
      {
         uint32 *ptr = (uint32 *)(mBase + y*mStride) + x0;
         for(int x=x0;x<x1;x++)
            *ptr++ = rgb.ival;
      }
   }
   else
   {
      for(int y=y0;y<y1;y++)
      {
         uint8 *ptr = (uint8 *)(mBase + y*mStride) + x0*pix_size;
         memset(ptr, 0, (x1-x0)*pix_size);
      }
   }

   if (mTexture)
      mTexture->Dirty( Rect(x0,y0,x1-x0,y1-y0) );
}

void SimpleSurface::Zero()
{
   if (mBase)
      memset(mBase,0,mStride * mHeight);
}

void SimpleSurface::dispose()
{
   destroyHardwareSurface();
   if (mBase)
   {
      if (mBase[mStride * mHeight] != 69)
      {
         ELOG("Image write overflow");
      }
      delete [] mBase;
      mBase = NULL;
   }
}

uint8  *SimpleSurface::Edit(const Rect *inRect)
{
   if (!mBase)
      return 0;

   Rect r = inRect ? inRect->Intersect( Rect(0,0,mWidth,mHeight) ) : Rect(0,0,mWidth,mHeight);
   if (mTexture)
      mTexture->Dirty(r);
   mVersion++;
      return mBase;
}



RenderTarget SimpleSurface::BeginRender(const Rect &inRect,bool inForHitTest)
{
   if (!mBase)
      return RenderTarget();

   Rect r =  inRect.Intersect( Rect(0,0,mWidth,mHeight) );
   if (mTexture)
      mTexture->Dirty(r);
   mVersion++;
   return RenderTarget(r, mPixelFormat,mBase,mStride);
}

void SimpleSurface::EndRender()
{
}

Surface *SimpleSurface::clone()
{
   SimpleSurface *copy = new SimpleSurface(mWidth,mHeight,mPixelFormat,1,pfNone);
   int pix_size = BytesPerPixel( mPixelFormat );
   if (mBase)
      for(int y=0;y<mHeight;y++)
         memcpy(copy->mBase + copy->mStride*y, mBase+mStride*y, mWidth*pix_size);
   
   copy->IncRef();
   return copy;
}

void SimpleSurface::getPixels(const Rect &inRect,uint32 *outPixels,bool inIgnoreOrder, bool inLittleEndian)
{
   if (!mBase)
      return;

   // PixelConvert

   Rect r = inRect.Intersect(Rect(0,0,Width(),Height()));

   ARGB *argb = (ARGB *)outPixels;
   for(int y=0;y<r.h;y++)
   {
      if (mPixelFormat==pfAlpha)
      {
         AlphaPixel *src = (AlphaPixel *)(mBase + (r.y+y)*mStride) + r.x;

         for(int x=0;x<r.w;x++)
            SetPixel(*argb++, *src++);
      }
      else if (mPixelFormat==pfRGB)
      {
         RGB *src = (RGB *)(mBase + (r.y+y)*mStride) + r.x;

         for(int x=0;x<r.w;x++)
            SetPixel(*argb++, *src++);
      }
      else if (inIgnoreOrder || inLittleEndian || mPixelFormat==pfBGRA)
      {
         ARGB *src = (ARGB *)(mBase + (r.y+y)*mStride) + r.x;
         memcpy(argb,src,r.w*4);
         argb+=r.w;
      }
      else if (mPixelFormat==pfBGRPremA)
      {
         BGRPremA *src = (BGRPremA *)(mBase + (r.y+y)*mStride) + r.x;

         for(int x=0;x<r.w;x++)
            SetPixel(*argb++, *src++);
      }
   }
}

void SimpleSurface::getColorBoundsRect(int inMask, int inCol, bool inFind, Rect &outRect)
{
   outRect = Rect();
   if (!mBase)
      return;

   int w = Width();
   int h = Height();

   if (w==0 || h==0 || mPixelFormat==pfAlpha || mPixelFormat>=pfRenderToCount)
      return;

   if (mPixelFormat==pfRGB && (inMask&0xff000000) && (inCol&0xff000000)!=0xff000000)
      return;

   int min_x = w + 1;
   int max_x = -1;
   int min_y = h + 1;
   int max_y = -1;

   ARGB argb(inCol);
   if (mPixelFormat==pfBGRPremA)
   {
      BGRPremA bgra;
      SetPixel(bgra, argb);
      argb.ival = bgra.ival;
   }
   argb.ival &= inMask;

   for(int y=0;y<h;y++)
   {
      if (mPixelFormat==pfRGB)
      {
         ARGB test;
         RGB *rgb = (RGB *)( mBase + y*mStride);
         for(int x=0;x<w;x++)
         {
            SetPixel(test,*rgb++);
            if ( ((test.ival&inMask)==inCol)==inFind )
            {
               if (x<min_x) min_x=x;
               if (x>max_x) max_x=x;
               if (y<min_y) min_y=y;
               if (y>max_y) max_y=y;
            }
         }

      }
      else
      {
         int *pixel = (int *)( mBase + y*mStride);
         for(int x=0;x<w;x++)
         {
            if ( (((*pixel++)&inMask)==inCol)==inFind )
            {
               if (x<min_x) min_x=x;
               if (x>max_x) max_x=x;
               if (y<min_y) min_y=y;
               if (y>max_y) max_y=y;
            }
         }
      }
   }

   if (min_x>max_x)
      outRect = Rect(0,0,0,0);
   else
      outRect = Rect(min_x,min_y,max_x-min_x+1,max_y-min_y+1);
}


void SimpleSurface::setPixels(const Rect &inRect,const uint32 *inPixels,bool inIgnoreOrder, bool inLittleEndian)
{
   if (!mBase)
      return;
   Rect r = inRect.Intersect(Rect(0,0,Width(),Height()));
   mVersion++;
   if (mTexture)
      mTexture->Dirty(r);

   PixelFormat convert = pfNone;
   // TODO - work out when auto-conversion is right
   if (!HasAlphaChannel(mPixelFormat))
   {
      int n = inRect.w * inRect.h;
      for(int i=0;i<n;i++)
         if ((inPixels[i]&0xff000000) != 0xff000000)
         {
            convert = pfBGRA;
            break;
         }
      if (convert==pfNone && mPixelFormat>=pfRenderToCount)
         convert = pfRGB;
   }
   else if (mPixelFormat>=pfRenderToCount)
      convert = pfBGRA;

   if (convert!=pfNone)
   {
      ChangeInternalFormat(convert, &r);
   }

   const ARGB *src = (const ARGB *)inPixels;

   for(int y=0;y<r.h;y++)
   {
      if (mPixelFormat==pfBGRA)
      {
         ARGB *dest = (ARGB *)(mBase + (r.y+y)*mStride) + r.x;
         memcpy(dest, src, r.w*sizeof(ARGB));
         src+=r.w;
      }
      else if (mPixelFormat==pfAlpha)
      {
         AlphaPixel *dest = (AlphaPixel *)(mBase + (r.y+y)*mStride) + r.x;
         for(int x=0;x<r.w;x++)
            SetPixel(*dest++,*src++);
      }
      else if (mPixelFormat==pfRGB)
      {
         RGB *dest = (RGB *)(mBase + (r.y+y)*mStride) + r.x;
         for(int x=0;x<r.w;x++)
            SetPixel(*dest++,*src++);
      }
      else if (mPixelFormat==pfBGRPremA)
      {
         BGRPremA *dest = (BGRPremA *)(mBase + (r.y+y)*mStride) + r.x;
         for(int x=0;x<r.w;x++)
            SetPixel(*dest++,*src++);
      }
   }
}

uint32 SimpleSurface::getPixel(int inX,int inY)
{
   if (inX<0 || inY<0 || inX>=mWidth || inY>=mHeight || !mBase)
      return 0;

   ARGB result(0xff000000);
   void *ptr = mBase + inY*mStride;
   switch(mPixelFormat)
   {
      case pfRGB: SetPixel(result, ((RGB *)ptr)[inX]); break;
      case pfBGRA: SetPixel(result, ((ARGB *)ptr)[inX]); break;
      case pfBGRPremA: SetPixel(result, ((BGRPremA *)ptr)[inX]); break;
      case pfAlpha: SetPixel(result, ((AlphaPixel *)ptr)[inX]); break;

      /* TODO
      case pfARGB4444:
      case pfRGB565:
      case pfLuma:
      case pfLumaAlpha:
      case pfECT:
      case pfRGB32f:
      case pfRGBA32f:
      case pfYUV420sp:
      case pfNV12:
      case pfOES:
      */
   }


   return result.ival;
}

void SimpleSurface::setPixel(int inX,int inY,uint32 inRGBA,bool inAlphaToo)
{
   if (inX<0 || inY<0 || inX>=mWidth || inY>=mHeight || !mBase)
      return;

   mVersion++;
   if (mTexture)
      mTexture->Dirty(Rect(inX,inY,1,1));

   if (inAlphaToo && ((inRGBA&0xff000000)!=0xff000000) && !HasAlphaChannel(mPixelFormat) )
      ChangeInternalFormat(pfBGRA);

   ARGB value(inRGBA);
   void *ptr = mBase + inY*mStride;
   switch(mPixelFormat)
   {
      case pfRGB: SetPixel(((RGB *)ptr)[inX],value); break;
      case pfBGRA: SetPixel(((ARGB *)ptr)[inX],value); break;
      case pfBGRPremA: SetPixel(((BGRPremA *)ptr)[inX],value); break;
      case pfAlpha: SetPixel(((AlphaPixel *)ptr)[inX],value); break;

      /* TODO
      case pfARGB4444:
      case pfRGB565:
      case pfLuma:
      case pfLumaAlpha:
      case pfECT:
      case pfRGB32f:
      case pfRGBA32f:
      case pfYUV420sp:
      case pfNV12:
      case pfOES:
      */
   }
}

void SimpleSurface::scroll(int inDX,int inDY)
{
   if ((inDX==0 && inDY==0) || !mBase) return;

   Rect src(0,0,mWidth,mHeight);
   src = src.Intersect( src.Translated(inDX,inDY) ).Translated(-inDX,-inDY);
   int pixels = src.Area();
   if (!pixels)
      return;

   uint32 *buffer = (uint32 *)malloc( pixels * sizeof(int) );
   getPixels(src,buffer,true);
   src.Translate(inDX,inDY);
   setPixels(src,buffer,true);
   free(buffer);
   mVersion++;
   if (mTexture)
      mTexture->Dirty(src);
}

void SimpleSurface::applyFilter(Surface *inSrc, const Rect &inRect, ImagePoint inOffset, Filter *inFilter)
{
   if (!mBase) return;
   FilterList f;
   f.push_back(inFilter);

   Rect src_rect(inRect.w,inRect.h);
   Rect dest = GetFilteredObjectRect(f,src_rect);

   inSrc->IncRef();
   Surface *result = FilterBitmap(f, inSrc, src_rect, dest, false, ImagePoint(inRect.x,inRect.y) );

   dest.Translate(inOffset.x, inOffset.y);

   src_rect = Rect(0,0,result->Width(),result->Height());
   int dx = dest.x;
   int dy = dest.y;
   dest = dest.Intersect( Rect(0,0,mWidth,mHeight) );
   dest.Translate(-dx,-dy);
   dest = dest.Intersect( src_rect );
   dest.Translate(dx,dy);

   int bpp = BytesPP();

   RenderTarget t = BeginRender(dest,false);
   //printf("Copy back @ %d,%d %dx%d  + (%d,%d)\n", dest.x, dest.y, t.Width(), t.Height(), dx, dy);
   for(int y=0;y<t.Height();y++)
      memcpy((void *)(t.Row(y+dest.y)+(dest.x)*bpp), result->Row(y-dy)-dx*bpp, dest.w*bpp);

   EndRender();

   result->DecRef();
}

/* A MINSTD pseudo-random number generator.
 *
 * This generates a pseudo-random number sequence equivalent to std::minstd_rand0 from the C++11 standard library, which
 * is the generator that Flash uses to generate noise for BitmapData.noise().
 *
 * It is reimplemented here because std::minstd_rand0 is not available in earlier versions of C++.
 *
 * MINSTD was originally suggested in "A pseudo-random number generator for the System/360", P.A. Lewis, A.S. Goodman,
 * J.M. Miller, IBM Systems Journal, Vol. 8, No. 2, 1969, pp. 136-146 */
class MinstdGenerator
{
public:
   MinstdGenerator(unsigned int seed)
   {
      if (seed == 0) {
         x = 1U;
      } else {
         x = seed;
      }
   }

   unsigned int operator () ()
   {
      const unsigned int a = 16807U;
      const unsigned int m = (1U << 31) - 1;

      unsigned int lo = a * (x & 0xffffU);
      unsigned int hi = a * (x >> 16);
      lo += (hi & 0x7fffU) << 16;

      if (lo > m)
      {
         lo &= m;
         ++lo;
      }

      lo += hi >> 15;

      if (lo > m)
      {
         lo &= m;
         ++lo;
      }

      x = lo;

      return x;
   }

private:
   unsigned int x;
};

void SimpleSurface::noise(unsigned int randomSeed, unsigned int low, unsigned int high, int channelOptions, bool grayScale)
{
   if (!mBase)
      return;

   MinstdGenerator generator(randomSeed);

   RenderTarget target = BeginRender(Rect(0,0,mWidth,mHeight),false);
   ARGB tmpRgb;

   int range = high - low + 1;

   for (int y=0;y<mHeight;y++)
   {
      ARGB *rgb = ((ARGB *)target.Row(y));
      for(int x=0;x<mWidth;x++)
      {
         if (grayScale)
         {
            tmpRgb.r = tmpRgb.g = tmpRgb.b = low + generator() % (high - low + 1);
         }
         else
         {
            if (channelOptions & CHAN_RED)
               tmpRgb.r = low + generator() % range;
            else
               tmpRgb.r = 0;

            if (channelOptions & CHAN_GREEN)
               tmpRgb.g = low + generator() % range;
            else
               tmpRgb.g = 0;

            if (channelOptions & CHAN_BLUE)
               tmpRgb.b = low + generator() % range;
            else
               tmpRgb.b = 0;
         }

         if (channelOptions & CHAN_ALPHA)
            tmpRgb.a = low + generator() % range;
         else
            tmpRgb.a = 255;

         *rgb = tmpRgb;

         rgb++;
      }
   }
   
   EndRender();
}


void SimpleSurface::unmultiplyAlpha()
{
   if (!mBase)
      return;
   Rect r = Rect(0,0,mWidth,mHeight);
   mVersion++;
   if (mTexture)
      mTexture->Dirty(r);
   
   if (mPixelFormat==pfAlpha)
      return;
   int i = 0;
   int a;
   double unmultiply;
   
   for(int y=0;y<r.h;y++)
   {
      uint8 *dest = mBase + (r.y+y)*mStride + r.x*4;
      for(int x=0;x<r.w;x++)
      {
         a = *(dest + 3);
         if(a!=0)
         {
            unmultiply = 255.0/a;
            *dest = sgClamp0255[int((*dest) * unmultiply)];
            *(dest+1) = sgClamp0255[int(*(dest + 1) * unmultiply)];
            *(dest+2) = sgClamp0255[int(*(dest + 2) * unmultiply)];
         }
         dest += 4;
      }
   }
}



// --- HardwareSurface -------------------------------------------------------------

HardwareSurface::HardwareSurface(HardwareRenderer *inContext)
{
   mHardware = inContext;
   mHardware->IncRef();
}

HardwareSurface::~HardwareSurface()
{
   mHardware->DecRef();
}

Surface *HardwareSurface::clone()
{
   // This is not really a clone...
   Surface *copy = new HardwareSurface(mHardware);
   copy->IncRef();
   return copy;

}

void HardwareSurface::getPixels(const Rect &inRect, uint32 *outPixels,bool inIgnoreOrder)
{
   memset(outPixels,0,Width()*Height()*4);
}

void HardwareSurface::setPixels(const Rect &inRect,const uint32 *outPixels,bool inIgnoreOrder)
{
}



// --- BitmapCache -----------------------------------------------------------------

const uint8 *BitmapCache::Row(int inRow) const
{
   return mBitmap->Row(inRow);
}


const uint8 *BitmapCache::DestRow(int inRow) const
{
   return mBitmap->Row(inRow-(mRect.y+mTY)) - mBitmap->BytesPP()*(mRect.x+mTX);
}


PixelFormat BitmapCache::Format() const
{
   return mBitmap->Format();
}

} // end namespace nme

