#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

struct TrackPoint {
    double lat;
    double lon;
    double ele;
    double dist;
    double asc;
    double desc;
    double t;
    int hr;
    double mx;
    double my;
    double x3d;
    double y3d;
    double z3d;
};

struct Vec3 {
    double x, y, z;
};

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 vec3_add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 vec3_scale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static inline double vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
static inline Vec3 vec3_norm(Vec3 a) {
    double len = std::sqrt(vec3_dot(a, a));
    if (len < 1e-9) return {0, 0, 0};
    return {a.x / len, a.y / len, a.z / len};
}

static double transformLat(double x, double y) {
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::abs(x));
    ret += (20.0 * std::sin(6.0 * x * M_PI) + 20.0 * std::sin(2.0 * x * M_PI)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * M_PI) + 40.0 * std::sin(y / 3.0 * M_PI)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * M_PI) + 320.0 * std::sin(y * M_PI / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transformLon(double x, double y) {
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::abs(x));
    ret += (20.0 * std::sin(6.0 * x * M_PI) + 20.0 * std::sin(2.0 * x * M_PI)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * M_PI) + 40.0 * std::sin(x / 3.0 * M_PI)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * M_PI) + 300.0 * std::sin(x / 30.0 * M_PI)) * 2.0 / 3.0;
    return ret;
}

static void wgs84_to_gcj02(double wgLat, double wgLon, double &mgLat, double &mgLon) {
    const double a = 6378245.0;
    const double ee = 0.00669342162296594323;
    double dLat = transformLat(wgLon - 105.0, wgLat - 35.0);
    double dLon = transformLon(wgLon - 105.0, wgLat - 35.0);
    double radLat = wgLat / 180.0 * M_PI;
    double magic = std::sin(radLat);
    magic = 1.0 - ee * magic * magic;
    double sqrtMagic = std::sqrt(magic);
    dLat = (dLat * 180.0) / ((a * (1.0 - ee)) / (magic * sqrtMagic) * M_PI);
    dLon = (dLon * 180.0) / (a / sqrtMagic * std::cos(radLat) * M_PI);
    mgLat = wgLat + dLat;
    mgLon = wgLon + dLon;
}

static void latlon_to_tile_frac(double lat, double lon, int z, double &tx, double &ty) {
    double n = (double)(1 << z);
    tx = (lon + 180.0) / 360.0 * n;
    double lat_rad = lat * M_PI / 180.0;
    ty = (1.0 - std::asinh(std::tan(lat_rad)) / M_PI) / 2.0 * n;
}

static void drawText(CGContextRef ctx, NSString *text, NSString *fontName, CGFloat fontSize, CGColorRef color, CGPoint pos, int align, bool shadow = true) {
    CTFontRef font = CTFontCreateWithName((__bridge CFStringRef)fontName, fontSize, NULL);
    NSDictionary *attrs = @{
        (__bridge id)kCTFontAttributeName: (__bridge id)font,
        (__bridge id)kCTForegroundColorAttributeName: (__bridge id)color
    };
    NSAttributedString *attrStr = [[NSAttributedString alloc] initWithString:text attributes:attrs];
    CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attrStr);
    CGFloat ascent = 0, descent = 0, leading = 0;
    double lineWidth = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

    CGFloat drawX = pos.x;
    if (align == 1) drawX -= lineWidth * 0.5;
    else if (align == 2) drawX -= lineWidth;

    CGContextSaveGState(ctx);
    if (shadow) {
        CGColorRef shadowCol = CGColorCreateGenericRGB(0, 0, 0, 0.75);
        CGContextSetShadowWithColor(ctx, CGSizeMake(1.5, -1.5), 3.0, shadowCol);
        CGColorRelease(shadowCol);
    }
    CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
    CGContextSetTextPosition(ctx, drawX, pos.y);
    CTLineDraw(line, ctx);
    CGContextRestoreGState(ctx);

    CFRelease(line);
    CFRelease(font);
}

static void fillRoundedRect(CGContextRef ctx, CGRect rect, CGFloat radius, CGColorRef fillColor, CGColorRef strokeColor = NULL, CGFloat strokeWidth = 1.0) {
    CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, NULL);
    CGContextSaveGState(ctx);
    if (fillColor) {
        CGContextSetFillColorWithColor(ctx, fillColor);
        CGContextAddPath(ctx, path);
        CGContextFillPath(ctx);
    }
    if (strokeColor) {
        CGContextSetStrokeColorWithColor(ctx, strokeColor);
        CGContextSetLineWidth(ctx, strokeWidth);
        CGContextAddPath(ctx, path);
        CGContextStrokePath(ctx);
    }
    CGContextRestoreGState(ctx);
    CFRelease(path);
}

// Activity icon modes: 0 = person (walk/run), 1 = bike (cycling), 2 = car (driving)
static void drawActivityMarker(CGContextRef ctx, CGFloat hx, CGFloat hy, CGFloat headingRad, CGFloat scale, int iconMode,
                               CGImageRef personImg, CGImageRef bikeImg, CGImageRef carImg) {
    if (iconMode == 0 && personImg) {
        // Walking / Running icon: upright standing figure with glowing ground ring at feet
        CGContextSaveGState(ctx);
        CGColorRef haloOuter = CGColorCreateGenericRGB(1.0, 1.0, 1.0, 0.45);
        CGColorRef dotCore   = CGColorCreateGenericRGB(0.80, 1.0, 0.18, 0.90);
        CGContextSetFillColorWithColor(ctx, haloOuter);
        CGContextFillEllipseInRect(ctx, CGRectMake(hx - 12 * scale, hy - 6 * scale, 24 * scale, 12 * scale));
        CGContextSetFillColorWithColor(ctx, dotCore);
        CGContextFillEllipseInRect(ctx, CGRectMake(hx - 7 * scale, hy - 3.5 * scale, 14 * scale, 7 * scale));
        CGColorRelease(haloOuter);
        CGColorRelease(dotCore);

        CGFloat pw = 24.0 * scale, ph = 48.0 * scale;
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextDrawImage(ctx, CGRectMake(hx - pw * 0.5, hy - 3.0 * scale, pw, ph), personImg);
        CGContextRestoreGState(ctx);
        return;
    }

    if (iconMode == 1 && bikeImg) {
        // Cycling icon: rotated along direction of travel
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, hx, hy);
        CGContextRotateCTM(ctx, headingRad - M_PI_2);
        CGContextScaleCTM(ctx, scale, scale);
        CGFloat bw = 36.0, bh = 43.0;
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextDrawImage(ctx, CGRectMake(-bw * 0.5, -bh * 0.5, bw, bh), bikeImg);
        CGContextRestoreGState(ctx);
        return;
    }

    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, hx, hy);
    CGContextRotateCTM(ctx, headingRad - M_PI_2);
    CGContextScaleCTM(ctx, scale, scale);

    if (carImg) {
        CGFloat carW = 24.0;
        CGFloat carH = 49.4; // Preserves exact 134:276 aspect ratio of car_icon.png
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGFloat shComps[] = {
            0.0, 0.0, 0.0, 0.38,
            0.0, 0.0, 0.0, 0.00
        };
        CGFloat shLocs[] = {0.0, 1.0};
        CGGradientRef shGrad = CGGradientCreateWithColorComponents(cs, shComps, shLocs, 2);
        CGContextSaveGState(ctx);
        CGContextScaleCTM(ctx, 1.0, 1.85);
        CGContextDrawRadialGradient(ctx, shGrad, CGPointZero, 2.0, CGPointZero, 15.5, 0);
        CGContextRestoreGState(ctx);
        CGGradientRelease(shGrad);
        CGColorSpaceRelease(cs);

        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextDrawImage(ctx, CGRectMake(-carW * 0.5, -carH * 0.5, carW, carH), carImg);
    }

    CGContextRestoreGState(ctx);
}

// Smooth velocity control curve u in [0, 1] -> distance fraction s in [0, 1]
static double controlledDistanceFrac(double u) {
    u = std::max(0.0, std::min(1.0, u));
    const double a = 0.07; // 7% ramp-up and ramp-down
    const double vmax = 1.0 / (1.0 - a);
    if (u < a) {
        return 0.5 * vmax * (u * u) / a;
    } else if (u <= 1.0 - a) {
        return vmax * (u - 0.5 * a);
    } else {
        double dt = 1.0 - u;
        return 1.0 - 0.5 * vmax * (dt * dt) / a;
    }
}

static CGImageRef loadPngImage(NSString *path) {
    if (!path) return NULL;
    NSURL *url = [NSURL fileURLWithPath:path];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    if (!src) return NULL;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    return img;
}

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        bool isPortrait = false;
        bool previewOnly = false;
        int forcedIconMode = -1;
        NSString *jsonPath = nil;
        NSString *resDir = nil;
        NSString *outVideoPath = nil;
        NSString *previewDir = nil;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--9x16" || arg == "9x16") isPortrait = true;
            else if (arg == "--16x9" || arg == "16x9") isPortrait = false;
            else if (arg == "--person") forcedIconMode = 0;
            else if (arg == "--bike") forcedIconMode = 1;
            else if (arg == "--car") forcedIconMode = 2;
            else if (arg == "--json" && i + 1 < argc) jsonPath = [NSString stringWithUTF8String:argv[++i]];
            else if (arg == "--res-dir" && i + 1 < argc) resDir = [NSString stringWithUTF8String:argv[++i]];
            else if (arg == "--out" && i + 1 < argc) outVideoPath = [NSString stringWithUTF8String:argv[++i]];
            else if (arg == "--preview" && i + 1 < argc) {
                previewOnly = true;
                previewDir = [NSString stringWithUTF8String:argv[++i]];
            }
        }

        if (!jsonPath || (!outVideoPath && !previewOnly)) {
            NSLog(@"Usage: render_3d_track --json <track_data.json> --res-dir <resources_dir> --out <out.mp4> [--16x9|--9x16] [--person|--bike|--car]");
            return 1;
        }

        NSData *jsonData = [NSData dataWithContentsOfFile:jsonPath];
        if (!jsonData) {
            NSLog(@"Failed to read %@", jsonPath);
            return 1;
        }
        NSDictionary *trackDict = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:nil];
        NSString *trackName = trackDict[@"name"] ?: @"3D运动轨迹";
        int jsonIconMode = trackDict[@"icon_mode"] ? [trackDict[@"icon_mode"] intValue] : -1;
        int avgHr = trackDict[@"avg_hr"] ? [trackDict[@"avg_hr"] intValue] : 0;
        int z = [trackDict[@"z"] intValue];
        int xMin = [trackDict[@"x_min"] intValue];
        int xMax = [trackDict[@"x_max"] intValue];
        int yMin = [trackDict[@"y_min"] intValue];
        int yMax = [trackDict[@"y_max"] intValue];
        bool useGcj02 = trackDict[@"use_gcj02"] ? [trackDict[@"use_gcj02"] boolValue] : false;
        bool useRoadOverlay = trackDict[@"use_road_overlay"] ? [trackDict[@"use_road_overlay"] boolValue] : false;
        NSString *tilesDir = trackDict[@"sat_tiles_dir"];
        NSString *roadTilesDir = trackDict[@"road_tiles_dir"];
        double totalDist = [trackDict[@"total_dist"] doubleValue];
        double totalAsc = [trackDict[@"total_asc"] doubleValue];
        double minEle = [trackDict[@"min_ele"] doubleValue];
        double maxEle = [trackDict[@"max_ele"] doubleValue];
        NSArray *ptsArr = trackDict[@"points"];

        if (!resDir) {
            NSString *execDir = [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent];
            resDir = [[execDir stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"resources"];
        }
        CGImageRef personIconImg = loadPngImage([resDir stringByAppendingPathComponent:@"person_icon.png"]);
        CGImageRef bikeIconImg   = loadPngImage([resDir stringByAppendingPathComponent:@"bike_icon.png"]);
        CGImageRef carIconImg    = loadPngImage([resDir stringByAppendingPathComponent:@"car_icon.png"]);

        const int tileW = 256, tileH = 256;
        const int cols = xMax - xMin + 1;
        const int rows = yMax - yMin + 1;
        const int mosaicW = cols * tileW;
        const int mosaicH = rows * tileH;

        NSLog(@"Stitching %dx%d zoom-%d satellite base mosaic (%dx%d px)...", cols, rows, z, mosaicW, mosaicH);
        std::vector<uint8_t> mosaicBuf(mosaicW * mosaicH * 4, 0);
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef mosaicCtx = CGBitmapContextCreate(
            mosaicBuf.data(), mosaicW, mosaicH, 8, mosaicW * 4, colorSpace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
        );

        for (int y = yMin; y <= yMax; y++) {
            for (int x = xMin; x <= xMax; x++) {
                NSString *tilePath = [NSString stringWithFormat:@"%@/%d_%d_%d.jpg", tilesDir, z, x, y];
                NSURL *tileUrl = [NSURL fileURLWithPath:tilePath];
                CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)tileUrl, NULL);
                if (src) {
                    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
                    if (img) {
                        int col = x - xMin;
                        int rowFromBottom = yMax - y;
                        CGRect rect = CGRectMake(col * tileW, rowFromBottom * tileH, tileW, tileH);
                        CGContextDrawImage(mosaicCtx, rect, img);
                        CGImageRelease(img);
                    }
                    CFRelease(src);
                }
            }
        }

        // Compute Web Mercator mosaic pixel coordinates for all points
        std::vector<TrackPoint> pts;
        pts.reserve(ptsArr.count);
        double minMx = 1e9, maxMx = -1e9, minMy = 1e9, maxMy = -1e9;
        for (NSDictionary *d in ptsArr) {
            TrackPoint tp;
            tp.lat = [d[@"lat"] doubleValue];
            tp.lon = [d[@"lon"] doubleValue];
            tp.ele = [d[@"ele"] doubleValue];
            tp.dist = [d[@"dist"] doubleValue];
            tp.asc = [d[@"asc"] doubleValue];
            tp.desc = [d[@"desc"] doubleValue];
            tp.t = [d[@"t"] doubleValue];
            tp.hr = [d[@"hr"] intValue];
            double mLat = tp.lat, mLon = tp.lon, tx, ty;
            if (useGcj02) {
                wgs84_to_gcj02(tp.lat, tp.lon, mLat, mLon);
            }
            latlon_to_tile_frac(mLat, mLon, z, tx, ty);
            tp.mx = (tx - xMin) * tileW;
            tp.my = (ty - yMin) * tileH;
            minMx = std::min(minMx, tp.mx);
            maxMx = std::max(maxMx, tp.mx);
            minMy = std::min(minMy, tp.my);
            maxMy = std::max(maxMy, tp.my);
            pts.push_back(tp);
        }

        double centerMx = 0.5 * (minMx + maxMx);
        double centerMy = 0.5 * (minMy + maxMy);
        double spanMx = std::max(200.0, maxMx - minMx);
        double spanMy = std::max(200.0, maxMy - minMy);
        double routeSpan = std::max(spanMx, spanMy);

        for (auto &tp : pts) {
            tp.x3d = (tp.mx - centerMx);
            tp.y3d = -(tp.my - centerMy);
            tp.z3d = 0.0;
        }

        // Build High-Resolution 3D Terrain Heightfield (320 x 512 grid covering the mosaic)
        NSLog(@"Building 3D terrain heightfield...");
        const int gridCols = 320;
        const int gridRows = 512;
        std::vector<double> gridZ(gridCols * gridRows, 0.0);
        std::vector<double> gridShade(gridCols * gridRows, 1.0);

        std::vector<double> ptSmoothEle(pts.size(), 0.0);
        for (size_t i = 0; i < pts.size(); i++) {
            double wSum = 0.0, eSum = 0.0;
            for (int k = -35; k <= 35; k++) {
                int idx = std::max(0, std::min((int)pts.size() - 1, (int)i + k));
                double w = std::exp(-0.5 * (k / 14.0) * (k / 14.0));
                wSum += w;
                eSum += w * pts[idx].ele;
            }
            ptSmoothEle[i] = eSum / wSum;
        }

        // Normalize elevation relief amplitude relative to routeSpan so both 5 km park runs and 1200 km road trips have balanced 3D hills
        double eleRange = std::max(15.0, maxEle - minEle);
        double eleScale = std::min(0.45, std::max(0.08, (routeSpan * 0.032) / eleRange));
        double mountainScale = std::max(0.35, std::min(1.0, routeSpan / 3200.0));

        for (int gy = 0; gy < gridRows; gy++) {
            double my = (double)gy / (gridRows - 1) * (mosaicH - 1);
            double y3d = -(my - centerMy);
            int iv = std::max(0, std::min(mosaicH - 1, (int)my));
            for (int gx = 0; gx < gridCols; gx++) {
                double mx = (double)gx / (gridCols - 1) * (mosaicW - 1);
                double x3d = (mx - centerMx);
                int iu = std::max(0, std::min(mosaicW - 1, (int)mx));

                double wSumTight = 0.0, zSumTight = 0.0;
                double wSumBroad = 0.0, zSumBroad = 0.0;
                double minDist2 = 1e18;
                size_t step = std::max((size_t)1, pts.size() / 600);
                for (size_t i = 0; i < pts.size(); i += step) {
                    double dx = x3d - pts[i].x3d;
                    double dy = y3d - pts[i].y3d;
                    double d2 = dx * dx + dy * dy;
                    if (d2 < minDist2) {
                        minDist2 = d2;
                    }
                    double wTight = 1.0 / (d2 + 180.0);
                    wSumTight += wTight;
                    double localDelta = pts[i].ele - ptSmoothEle[i];
                    double enhancedEle = (pts[i].ele - minEle) * eleScale + localDelta * (eleScale * 1.1);
                    zSumTight += wTight * enhancedEle;

                    double wBroad = 1.0 / (d2 + 3200.0);
                    wSumBroad += wBroad;
                    zSumBroad += wBroad * (pts[i].ele - minEle) * (eleScale * 1.1);
                }
                double gpEleTight = (wSumTight > 0) ? (zSumTight / wSumTight) : 0.0;
                double gpEleBroad = (wSumBroad > 0) ? (zSumBroad / wSumBroad) : 0.0;
                double distToRoute = std::sqrt(minDist2);

                const uint8_t *tex = mosaicBuf.data() + (iv * mosaicW + iu) * 4;
                double r = tex[0], g = tex[1], b = tex[2];
                double greenNess = std::max(0.0, g - 0.54 * r - 0.44 * b);
                double luma = 0.299 * r + 0.587 * g + 0.114 * b;
                double darkForest = std::max(0.0, 102.0 - luma);
                double satMountainSignal = std::max(0.0, 1.10 * greenNess + 0.90 * darkForest - 8.0);

                double p1 = std::sin(x3d * 0.029 + y3d * 0.013);
                double p2 = std::sin(x3d * 0.011 - y3d * 0.031 + 1.7);
                double p3 = std::sin(x3d * 0.023 - y3d * 0.022 + 3.1);
                double p4 = std::sin(x3d * 0.017 + y3d * 0.027 + 0.8);
                double isoPeaks = 0.65 + 0.35 * (0.25 * (p1 + p2 + p3 + p4));

                double rawMountainH = satMountainSignal * isoPeaks * 0.85 * mountainScale;
                double sideHillFactor = 1.0 - 0.86 * std::exp(-(distToRoute * distToRoute) / (2.0 * 48.0 * 48.0));

                double corridorUndulation = (4.2 * std::sin(y3d * 0.016) * std::cos(x3d * 0.014 + 0.3)
                                          + 2.4 * std::sin(y3d * 0.036 + 1.1)) * mountainScale;
                double routeCorridorWeight = std::exp(-(distToRoute * distToRoute) / (2.0 * 85.0 * 85.0));

                double zVal = (gpEleTight + corridorUndulation) * routeCorridorWeight
                            + gpEleBroad * (1.0 - routeCorridorWeight)
                            + rawMountainH * sideHillFactor;
                gridZ[gy * gridCols + gx] = std::max(0.0, zVal);
            }
        }

        // Pre-render the static 2D overview minimap image for the top-left HUD widget (from clean satellite base)
        int miniW = isPortrait ? 190 : 210;
        int miniH = isPortrait ? 200 : 195;
        double padMx = std::max(30.0, (maxMx - minMx) * 0.25);
        double padMy = std::max(30.0, (maxMy - minMy) * 0.15);
        double boxW = (maxMx - minMx) + 2 * padMx;
        double boxH = (maxMy - minMy) + 2 * padMy;
        if (boxW / boxH > (double)miniW / miniH) {
            double newH = boxW * miniH / miniW;
            padMy += (newH - boxH) * 0.5;
            boxH = newH;
        } else {
            double newW = boxH * miniW / miniH;
            padMx += (newW - boxW) * 0.5;
            boxW = newW;
        }
        double cropX0 = minMx - padMx;
        double cropY0 = minMy - padMy;

        std::vector<uint8_t> miniBgBuf(miniW * miniH * 4, 0);
        auto mirrorMini = [](int val, int limit) -> int {
            if (val < 0) val = -val;
            int period = std::max(1, 2 * (limit - 1));
            val = val % period;
            if (val >= limit) val = period - val;
            return std::max(0, std::min(limit - 1, val));
        };
        for (int py = 0; py < miniH; py++) {
            double my = cropY0 + (py / (double)(miniH - 1)) * boxH;
            int iv = mirrorMini((int)my, mosaicH);
            for (int px = 0; px < miniW; px++) {
                double mx = cropX0 + (px / (double)(miniW - 1)) * boxW;
                int iu = mirrorMini((int)mx, mosaicW);
                const uint8_t *tex = mosaicBuf.data() + (iv * mosaicW + iu) * 4;
                int lum = (int)(0.299 * tex[0] + 0.587 * tex[1] + 0.114 * tex[2]);
                uint8_t c = (uint8_t)std::min(255, std::max(0, (int)(lum * 0.85 + 25)));
                uint8_t *dst = miniBgBuf.data() + (py * miniW + px) * 4;
                dst[0] = c;
                dst[1] = c;
                dst[2] = c;
                dst[3] = 175;
            }
        }
        CGContextRef miniBgCtx = CGBitmapContextCreate(
            miniBgBuf.data(), miniW, miniH, 8, miniW * 4, colorSpace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
        );
        CGImageRef miniBgImage = CGBitmapContextCreateImage(miniBgCtx);

        // Composite the Hybrid Road, Highway Shield & Place Name Overlay onto the Satellite Mosaic (if enabled)
        if (useRoadOverlay && roadTilesDir) {
            NSLog(@"Compositing highway & road network overlay tiles onto satellite mosaic...");
            CGContextSaveGState(mosaicCtx);
            CGColorRef dimOverlay = CGColorCreateGenericRGB(0.04, 0.06, 0.09, 0.08);
            CGContextSetFillColorWithColor(mosaicCtx, dimOverlay);
            CGContextFillRect(mosaicCtx, CGRectMake(0, 0, mosaicW, mosaicH));
            CGColorRelease(dimOverlay);

            CGContextSetAlpha(mosaicCtx, 0.94);
            int roadTilesLoaded = 0;
            for (int y = yMin; y <= yMax; y++) {
                for (int x = xMin; x <= xMax; x++) {
                    NSString *rTilePath = [NSString stringWithFormat:@"%@/%d_%d_%d.png", roadTilesDir, z, x, y];
                    NSURL *rTileUrl = [NSURL fileURLWithPath:rTilePath];
                    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)rTileUrl, NULL);
                    if (src) {
                        CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
                        if (img) {
                            int col = x - xMin;
                            int rowFromBottom = yMax - y;
                            CGRect rect = CGRectMake(col * tileW, rowFromBottom * tileH, tileW, tileH);
                            CGContextDrawImage(mosaicCtx, rect, img);
                            CGImageRelease(img);
                            roadTilesLoaded++;
                        }
                        CFRelease(src);
                    }
                }
            }
            CGContextRestoreGState(mosaicCtx);
            NSLog(@"Composited %d road overlay tiles onto satellite mosaic.", roadTilesLoaded);
        }
        CGImageRef mosaicCGImage = CGBitmapContextCreateImage(mosaicCtx);

        // Smooth the terrain grid (2 passes of 3x3 Gaussian-like filter)
        std::vector<double> tempZ = gridZ;
        for (int pass = 0; pass < 2; pass++) {
            for (int gy = 1; gy < gridRows - 1; gy++) {
                for (int gx = 1; gx < gridCols - 1; gx++) {
                    double sum = 0.0, wSum = 0.0;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            double w = (dx == 0 && dy == 0) ? 4.0 : ((dx == 0 || dy == 0) ? 2.0 : 1.0);
                            sum += w * gridZ[(gy + dy) * gridCols + (gx + dx)];
                            wSum += w;
                        }
                    }
                    tempZ[gy * gridCols + gx] = sum / wSum;
                }
            }
            gridZ = tempZ;
        }

        // Low-angle directional sunlight from West-Northwest to cast strong 3D hill & valley relief shadows
        Vec3 sunDir = vec3_norm({-0.72, 0.32, 0.62});
        for (int gy = 0; gy < gridRows; gy++) {
            for (int gx = 0; gx < gridCols; gx++) {
                int gx0 = std::max(0, gx - 1), gx1 = std::min(gridCols - 1, gx + 1);
                int gy0 = std::max(0, gy - 1), gy1 = std::min(gridRows - 1, gy + 1);
                double dx = (gx1 - gx0) * ((double)mosaicW / (gridCols - 1));
                double dy = (gy1 - gy0) * ((double)mosaicH / (gridRows - 1));
                double dzdx = (gridZ[gy * gridCols + gx1] - gridZ[gy * gridCols + gx0]) / dx;
                double dzdy = (gridZ[gy0 * gridCols + gx] - gridZ[gy1 * gridCols + gx]) / dy;
                Vec3 norm = vec3_norm({-dzdx * 2.4, -dzdy * 2.4, 1.0});
                double dot = vec3_dot(norm, sunDir);
                double elevBoost = std::min(0.12, gridZ[gy * gridCols + gx] / 1800.0);
                double shade = 0.54 + 0.64 * std::max(0.0, dot) + elevBoost;
                gridShade[gy * gridCols + gx] = std::min(1.28, std::max(0.50, shade));
            }
        }

        auto sampleTerrainZ = [&](double mx, double my) -> double {
            double u = std::max(0.0, std::min((double)(gridCols - 1), mx / (mosaicW - 1) * (gridCols - 1)));
            double v = std::max(0.0, std::min((double)(gridRows - 1), my / (mosaicH - 1) * (gridRows - 1)));
            int x0 = std::max(0, std::min(gridCols - 2, (int)u));
            int y0 = std::max(0, std::min(gridRows - 2, (int)v));
            double fx = u - x0;
            double fy = v - y0;
            double z00 = gridZ[y0 * gridCols + x0];
            double z10 = gridZ[y0 * gridCols + x0 + 1];
            double z01 = gridZ[(y0 + 1) * gridCols + x0];
            double z11 = gridZ[(y0 + 1) * gridCols + x0 + 1];
            return (1 - fx) * (1 - fy) * z00 + fx * (1 - fy) * z10 + (1 - fx) * fy * z01 + fx * fy * z11;
        };

        auto sampleTerrainShade = [&](double mx, double my) -> double {
            double u = std::max(0.0, std::min((double)(gridCols - 1), mx / (mosaicW - 1) * (gridCols - 1)));
            double v = std::max(0.0, std::min((double)(gridRows - 1), my / (mosaicH - 1) * (gridRows - 1)));
            int x0 = std::max(0, std::min(gridCols - 2, (int)u));
            int y0 = std::max(0, std::min(gridRows - 2, (int)v));
            double fx = u - x0;
            double fy = v - y0;
            double s00 = gridShade[y0 * gridCols + x0];
            double s10 = gridShade[y0 * gridCols + x0 + 1];
            double s01 = gridShade[(y0 + 1) * gridCols + x0];
            double s11 = gridShade[(y0 + 1) * gridCols + x0 + 1];
            return (1 - fx) * (1 - fy) * s00 + fx * (1 - fy) * s10 + (1 - fx) * fy * s01 + fx * fy * s11;
        };

        for (auto &tp : pts) {
            tp.z3d = sampleTerrainZ(tp.mx, tp.my) + 1.6;
        }
        std::vector<double> smoothTrackZ(pts.size(), 0.0);
        for (size_t i = 0; i < pts.size(); i++) {
            double wSum = 0.0, zSum = 0.0;
            for (int k = -12; k <= 12; k++) {
                int idx = std::max(0, std::min((int)pts.size() - 1, (int)i + k));
                double w = std::exp(-0.5 * (k / 5.0) * (k / 5.0));
                wSum += w;
                zSum += w * pts[idx].z3d;
            }
            smoothTrackZ[i] = zSum / wSum;
        }
        for (size_t i = 0; i < pts.size(); i++) {
            pts[i].z3d = smoothTrackZ[i];
        }

        // Determine final activity icon mode (0 = person, 1 = bike, 2 = car)
        double totalDurSec = std::max(1.0, pts.back().t - pts.front().t);
        double avgSpeedKmh = (totalDist / totalDurSec) * 3.6;
        int activeIconMode = (forcedIconMode >= 0) ? forcedIconMode :
                             ((jsonIconMode >= 0) ? jsonIconMode :
                             ((avgSpeedKmh < 12.0) ? 0 : (avgSpeedKmh <= 35.0 ? 1 : 2)));

        // Video Dimensions & Timing (56.5 seconds @ 30fps = 1695 frames)
        const int W = isPortrait ? 720 : 1280;
        const int H = isPortrait ? 1280 : 720;
        const int fps = 30;
        const int introFrames   = 4 * fps;
        const int zoomFrames    = 75;
        const int flyFrames     = 45 * fps;
        const int zoomOutFrames = 75;
        const int holdFrames    = 75;
        const int outroFrames   = zoomOutFrames + holdFrames;
        const int totalFrames   = introFrames + zoomFrames + flyFrames + outroFrames;

        AVAssetWriter *writer = nil;
        AVAssetWriterInput *writerInput = nil;
        AVAssetWriterInputPixelBufferAdaptor *adaptor = nil;

        if (!previewOnly) {
            [[NSFileManager defaultManager] removeItemAtPath:outVideoPath error:nil];
            NSURL *outUrl = [NSURL fileURLWithPath:outVideoPath];
            NSError *err = nil;
            writer = [[AVAssetWriter alloc] initWithURL:outUrl fileType:AVFileTypeMPEG4 error:&err];
            if (err || !writer) {
                NSLog(@"Failed to create AVAssetWriter: %@", err);
                return 1;
            }
            NSDictionary *videoSettings = @{
                AVVideoCodecKey: AVVideoCodecTypeH264,
                AVVideoWidthKey: @(W),
                AVVideoHeightKey: @(H),
                AVVideoCompressionPropertiesKey: @{
                    AVVideoAverageBitRateKey: @(12500000),
                    AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel
                }
            };
            writerInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
            writerInput.expectsMediaDataInRealTime = NO;

            NSDictionary *sourcePixelBufferAttributes = @{
                (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32ARGB),
                (NSString *)kCVPixelBufferWidthKey: @(W),
                (NSString *)kCVPixelBufferHeightKey: @(H)
            };
            adaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:writerInput
                                                                                       sourcePixelBufferAttributes:sourcePixelBufferAttributes];
            [writer addInput:writerInput];
            [writer startWriting];
            [writer startSessionAtSourceTime:kCMTimeZero];
        }

        std::vector<uint8_t> frameBuf(W * H * 4, 0);
        std::vector<float> zBuf(W * H, 1e30f);
        CGContextRef frameCtx = CGBitmapContextCreate(
            frameBuf.data(), W, H, 8, W * 4, colorSpace,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Big
        );

        auto getTrackPosAtFrac = [&](double frac, int &outIdx) -> TrackPoint {
            double targetDist = frac * totalDist;
            int lo = 0, hi = (int)pts.size() - 1;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (pts[mid].dist < targetDist) lo = mid + 1;
                else hi = mid;
            }
            outIdx = lo;
            if (lo == 0) return pts[0];
            const auto &p0 = pts[lo - 1];
            const auto &p1 = pts[lo];
            double seg = p1.dist - p0.dist;
            double alpha = (seg > 1e-6) ? (targetDist - p0.dist) / seg : 0.0;
            TrackPoint res = p0;
            res.lat = p0.lat + alpha * (p1.lat - p0.lat);
            res.lon = p0.lon + alpha * (p1.lon - p0.lon);
            res.ele = p0.ele + alpha * (p1.ele - p0.ele);
            res.dist = targetDist;
            res.asc = p0.asc + alpha * (p1.asc - p0.asc);
            res.desc = p0.desc + alpha * (p1.desc - p0.desc);
            res.mx = p0.mx + alpha * (p1.mx - p0.mx);
            res.my = p0.my + alpha * (p1.my - p0.my);
            res.x3d = p0.x3d + alpha * (p1.x3d - p0.x3d);
            res.y3d = p0.y3d + alpha * (p1.y3d - p0.y3d);
            res.z3d = p0.z3d + alpha * (p1.z3d - p0.z3d);
            return res;
        };

        // Dynamically scale overview camera distance/height to frame any route size cleanly
        double spanScale = routeSpan / 4500.0;
        double overviewDist = (isPortrait ? 2750.0 : 3350.0) * spanScale;
        double overviewHeight = (isPortrait ? 3150.0 : 3550.0) * spanScale;
        double chaseScale = std::max(0.32, std::min(1.0, spanScale));

        auto renderFrame = [&](int f) {
            std::fill(zBuf.begin(), zBuf.end(), 1e30f);
            for (int y = 0; y < H; y++) {
                double tSky = std::min(1.0, y / (H * 0.36));
                uint8_t r = (uint8_t)(26 + 34 * tSky);
                uint8_t g = (uint8_t)(42 + 42 * tSky);
                uint8_t b = (uint8_t)(66 + 44 * tSky);
                uint8_t *rowOut = frameBuf.data() + y * W * 4;
                for (int x = 0; x < W; x++) {
                    rowOut[x * 4 + 0] = 255;
                    rowOut[x * 4 + 1] = r;
                    rowOut[x * 4 + 2] = g;
                    rowOut[x * 4 + 3] = b;
                }
            }

            Vec3 camPos, lookAt;
            double fovY = (isPortrait ? 64.0 : 50.0) * M_PI / 180.0;
            double distFrac = 0.0;
            int curIdx = 0;
            TrackPoint curPt = pts[0];
            bool showHUD = false;
            double hudAlpha = 0.0;
            bool showOverviewRoute = false;
            double overviewAlpha = 0.0;

            double overviewAngle = -0.20 + 0.30 * ((double)f / (introFrames + zoomFrames));
            Vec3 overviewTarget = {0.0, (isPortrait ? -140.0 : 40.0) * spanScale, 35.0 * chaseScale};
            Vec3 overviewCam = {
                overviewTarget.x - overviewDist * std::sin(overviewAngle),
                overviewTarget.y - overviewDist * std::cos(overviewAngle),
                overviewHeight
            };

            auto computeFlyCam = [&](double dFrac, Vec3 &outCam, Vec3 &outTarget, TrackPoint &outPt, int &outIdx) {
                outPt = getTrackPosAtFrac(dFrac, outIdx);
                Vec3 avgPos = {0, 0, 0};
                double wTotal = 0;
                for (int k = -16; k <= 20; k++) {
                    double fk = std::max(0.0, std::min(1.0, dFrac + k * 0.0042));
                    int dummy;
                    TrackPoint pk = getTrackPosAtFrac(fk, dummy);
                    double w = std::exp(-0.5 * (k / 8.0) * (k / 8.0));
                    avgPos.x += w * pk.x3d;
                    avgPos.y += w * pk.y3d;
                    avgPos.z += w * pk.z3d;
                    wTotal += w;
                }
                avgPos = vec3_scale(avgPos, 1.0 / wTotal);

                int dummyAhead;
                TrackPoint pAhead = getTrackPosAtFrac(std::min(1.0, dFrac + 0.032), dummyAhead);
                TrackPoint pBehind = getTrackPosAtFrac(std::max(0.0, dFrac - 0.022), dummyAhead);
                double dxRoute = pAhead.x3d - pBehind.x3d;
                double dyRoute = std::max(80.0 * chaseScale, pAhead.y3d - pBehind.y3d);
                double routeHeading = std::atan2(dxRoute, dyRoute);

                TrackPoint pSlopeAhead  = getTrackPosAtFrac(std::min(1.0, dFrac + 0.014), dummyAhead);
                TrackPoint pSlopeBehind = getTrackPosAtFrac(std::max(0.0, dFrac - 0.010), dummyAhead);
                double slopeZ = pSlopeAhead.z3d - pSlopeBehind.z3d;

                double camAzimuth = routeHeading * 0.48 + 0.20 * std::sin(dFrac * M_PI * 3.0 - 0.35);

                double leadOffset = (isPortrait ? 90.0 : 80.0) * chaseScale;
                double targetZ = outPt.z3d + 0.22 * slopeZ;
                outTarget = {
                    outPt.x3d * 0.62 + avgPos.x * 0.38 + leadOffset * std::sin(camAzimuth),
                    outPt.y3d * 0.62 + avgPos.y * 0.38 + leadOffset * std::cos(camAzimuth),
                    targetZ
                };
                double camDist = (isPortrait ? 360.0 : 330.0) * chaseScale;
                double baseCamHeight = (isPortrait ? 185.0 : 162.0) * chaseScale;
                double camX = outTarget.x - camDist * std::sin(camAzimuth);
                double camY = outTarget.y - camDist * std::cos(camAzimuth);
                double camMx = std::max(0.0, std::min((double)(mosaicW - 1), camX + centerMx));
                double camMy = std::max(0.0, std::min((double)(mosaicH - 1), centerMy - camY));
                double camGroundZ = sampleTerrainZ(camMx, camMy);
                double camBaseZ = std::max(0.28 * outPt.z3d + 0.72 * avgPos.z - 0.10 * slopeZ, camGroundZ + 15.0 * chaseScale);
                outCam = {
                    camX,
                    camY,
                    camBaseZ + baseCamHeight
                };
            };

            auto smoothstep = [](double x) {
                x = std::max(0.0, std::min(1.0, x));
                return x * x * (3.0 - 2.0 * x);
            };

            if (f < introFrames) {
                camPos = overviewCam;
                lookAt = overviewTarget;
                showOverviewRoute = true;
                overviewAlpha = 1.0;
            } else if (f < introFrames + zoomFrames) {
                double u = smoothstep((double)(f - introFrames) / zoomFrames);
                Vec3 flyCam0, flyTarget0;
                computeFlyCam(0.0, flyCam0, flyTarget0, curPt, curIdx);
                camPos = vec3_add(vec3_scale(overviewCam, 1.0 - u), vec3_scale(flyCam0, u));
                lookAt = vec3_add(vec3_scale(overviewTarget, 1.0 - u), vec3_scale(flyTarget0, u));
                showOverviewRoute = (u < 0.7);
                overviewAlpha = std::max(0.0, 1.0 - u * 1.5);
                showHUD = (u > 0.5);
                hudAlpha = std::max(0.0, (u - 0.5) * 2.0);
            } else if (f < introFrames + zoomFrames + flyFrames) {
                double timeFrac = (double)(f - (introFrames + zoomFrames)) / (flyFrames - 1);
                distFrac = controlledDistanceFrac(timeFrac);
                computeFlyCam(distFrac, camPos, lookAt, curPt, curIdx);
                showHUD = true;
                hudAlpha = 1.0;
            } else {
                distFrac = 1.0;
                int outroF = f - (introFrames + zoomFrames + flyFrames);
                double uZoom = smoothstep((double)outroF / zoomOutFrames);
                double holdDrift = std::max(0.0, (double)(outroF - zoomOutFrames) / holdFrames);
                double outroAngle = 0.12 - 0.24 * (0.5 * uZoom + 0.5 * holdDrift);
                Vec3 outroOverviewCam = {
                    overviewTarget.x - overviewDist * std::sin(outroAngle),
                    overviewTarget.y - overviewDist * std::cos(outroAngle),
                    overviewHeight
                };

                Vec3 flyCam1, flyTarget1;
                computeFlyCam(1.0, flyCam1, flyTarget1, curPt, curIdx);
                camPos = vec3_add(vec3_scale(flyCam1, 1.0 - uZoom), vec3_scale(outroOverviewCam, uZoom));
                lookAt = vec3_add(vec3_scale(flyTarget1, 1.0 - uZoom), vec3_scale(overviewTarget, uZoom));

                showHUD = (uZoom < 0.75);
                hudAlpha = std::max(0.0, 1.0 - uZoom * 1.4);
                showOverviewRoute = (uZoom > 0.35);
                overviewAlpha = std::min(1.0, std::max(0.0, (uZoom - 0.35) / 0.55));
            }

            Vec3 forward = vec3_norm(vec3_sub(lookAt, camPos));
            Vec3 worldUp = {0.0, 0.0, 1.0};
            Vec3 right = vec3_norm(vec3_cross(forward, worldUp));
            Vec3 up = vec3_cross(right, forward);

            double focal = (H * 0.5) / std::tan(fovY * 0.5);
            double cx = W * 0.5;
            double cy = isPortrait ? (H * 0.45) : (H * 0.46);

            auto projectPoint = [&](Vec3 p, double &sx, double &sy, double &sz) -> bool {
                Vec3 rel = vec3_sub(p, camPos);
                double xCam = vec3_dot(rel, right);
                double yCam = vec3_dot(rel, up);
                double zCam = vec3_dot(rel, forward);
                sz = zCam;
                if (zCam <= 10.0 * chaseScale) return false;
                sx = cx + (xCam / zCam) * focal;
                sy = cy - (yCam / zCam) * focal;
                return true;
            };

            struct ProjVtx {
                float sx, sy, sz, invZ;
                float uOverZ, vOverZ, shadeOverZ;
                bool valid;
            };
            auto mapMeshCoord = [](double t, double expand) -> double {
                double c = t - 0.5;
                return 0.5 + c * (1.15 + expand * 4.0 * c * c);
            };
            std::vector<ProjVtx> vtx(gridCols * gridRows);
            for (int gy = 0; gy < gridRows; gy++) {
                double fracY = mapMeshCoord((double)gy / (gridRows - 1), 1.75);
                double my = fracY * (mosaicH - 1);
                double y3d = -(my - centerMy);
                double clampedMy = std::max(0.0, std::min((double)(mosaicH - 1), my));
                for (int gx = 0; gx < gridCols; gx++) {
                    double fracX = mapMeshCoord((double)gx / (gridCols - 1), 2.65);
                    double mx = fracX * (mosaicW - 1);
                    double x3d = (mx - centerMx);
                    double clampedMx = std::max(0.0, std::min((double)(mosaicW - 1), mx));
                    double z3d = sampleTerrainZ(clampedMx, clampedMy);
                    double shadeVal = sampleTerrainShade(clampedMx, clampedMy);
                    double sx, sy, sz;
                    bool ok = projectPoint({x3d, y3d, z3d}, sx, sy, sz);
                    ProjVtx &pv = vtx[gy * gridCols + gx];
                    pv.valid = ok;
                    if (ok) {
                        float invZ = (float)(1.0 / sz);
                        pv.sx = (float)sx;
                        pv.sy = (float)sy;
                        pv.sz = (float)sz;
                        pv.invZ = invZ;
                        pv.uOverZ = (float)mx * invZ;
                        pv.vOverZ = (float)my * invZ;
                        pv.shadeOverZ = (float)shadeVal * invZ;
                    }
                }
            }

            auto mirrorCoord = [](int val, int limit) -> int {
                if (val < 0) val = -val;
                int period = std::max(1, 2 * (limit - 1));
                val = val % period;
                if (val >= limit) val = period - val;
                return std::max(0, std::min(limit - 1, val));
            };

            auto rasterizeTri = [&](const ProjVtx &v0, const ProjVtx &v1, const ProjVtx &v2) {
                if (!v0.valid || !v1.valid || !v2.valid) return;
                float minX = std::min({v0.sx, v1.sx, v2.sx});
                float maxX = std::max({v0.sx, v1.sx, v2.sx});
                float minY = std::min({v0.sy, v1.sy, v2.sy});
                float maxY = std::max({v0.sy, v1.sy, v2.sy});
                if (maxX < 0 || minX >= W || maxY < 0 || minY >= H) return;

                int x0 = std::max(0, (int)std::floor(minX));
                int x1 = std::min(W - 1, (int)std::ceil(maxX));
                int y0 = std::max(0, (int)std::floor(minY));
                int y1 = std::min(H - 1, (int)std::ceil(maxY));

                float denom = (v1.sy - v2.sy) * (v0.sx - v2.sx) + (v2.sx - v1.sx) * (v0.sy - v2.sy);
                if (std::abs(denom) < 1e-4f) return;
                float invDenom = 1.0f / denom;

                for (int py = y0; py <= y1; py++) {
                    float fy = py + 0.5f;
                    uint8_t *rowOut = frameBuf.data() + py * W * 4;
                    float *rowZ = zBuf.data() + py * W;
                    for (int px = x0; px <= x1; px++) {
                        float fx = px + 0.5f;
                        float w0 = ((v1.sy - v2.sy) * (fx - v2.sx) + (v2.sx - v1.sx) * (fy - v2.sy)) * invDenom;
                        float w1 = ((v2.sy - v0.sy) * (fx - v2.sx) + (v0.sx - v2.sx) * (fy - v2.sy)) * invDenom;
                        float w2 = 1.0f - w0 - w1;
                        if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                            float invZ = w0 * v0.invZ + w1 * v1.invZ + w2 * v2.invZ;
                            float z = 1.0f / invZ;
                            if (z < rowZ[px]) {
                                rowZ[px] = z;
                                float u = (w0 * v0.uOverZ + w1 * v1.uOverZ + w2 * v2.uOverZ) * z;
                                float v = (w0 * v0.vOverZ + w1 * v1.vOverZ + w2 * v2.vOverZ) * z;
                                float shade = (w0 * v0.shadeOverZ + w1 * v1.shadeOverZ + w2 * v2.shadeOverZ) * z;

                                int uf = (int)std::floor(u);
                                int vf = (int)std::floor(v);
                                float fu = u - uf;
                                float fv = v - vf;
                                int iu0 = mirrorCoord(uf, mosaicW);
                                int iu1 = mirrorCoord(uf + 1, mosaicW);
                                int iv0 = mirrorCoord(vf, mosaicH);
                                int iv1 = mirrorCoord(vf + 1, mosaicH);

                                const uint8_t *t00 = mosaicBuf.data() + (iv0 * mosaicW + iu0) * 4;
                                const uint8_t *t10 = mosaicBuf.data() + (iv0 * mosaicW + iu1) * 4;
                                const uint8_t *t01 = mosaicBuf.data() + (iv1 * mosaicW + iu0) * 4;
                                const uint8_t *t11 = mosaicBuf.data() + (iv1 * mosaicW + iu1) * 4;

                                float w00 = (1.0f - fu) * (1.0f - fv);
                                float w10 = fu * (1.0f - fv);
                                float w01 = (1.0f - fu) * fv;
                                float w11 = fu * fv;

                                float tr = w00 * t00[0] + w10 * t10[0] + w01 * t01[0] + w11 * t11[0];
                                float tg = w00 * t00[1] + w10 * t10[1] + w01 * t01[1] + w11 * t11[1];
                                float tb = w00 * t00[2] + w10 * t10[2] + w01 * t01[2] + w11 * t11[2];

                                float fog = std::min(0.55f, std::max(0.0f, (z - (float)(1350.0 * chaseScale)) / (float)(3100.0 * chaseScale)));
                                int r = (int)((tr * shade) * (1.0f - fog) + 60.0f * fog);
                                int g = (int)((tg * shade) * (1.0f - fog) + 84.0f * fog);
                                int b = (int)((tb * shade) * (1.0f - fog) + 110.0f * fog);
                                rowOut[px * 4 + 0] = 255;
                                rowOut[px * 4 + 1] = (uint8_t)std::min(255, std::max(0, r));
                                rowOut[px * 4 + 2] = (uint8_t)std::min(255, std::max(0, g));
                                rowOut[px * 4 + 3] = (uint8_t)std::min(255, std::max(0, b));
                            }
                        }
                    }
                }
            };

            for (int gy = 0; gy < gridRows - 1; gy++) {
                for (int gx = 0; gx < gridCols - 1; gx++) {
                    const ProjVtx &v00 = vtx[gy * gridCols + gx];
                    const ProjVtx &v10 = vtx[gy * gridCols + gx + 1];
                    const ProjVtx &v01 = vtx[(gy + 1) * gridCols + gx];
                    const ProjVtx &v11 = vtx[(gy + 1) * gridCols + gx + 1];
                    rasterizeTri(v00, v10, v01);
                    rasterizeTri(v10, v11, v01);
                }
            }

            CGContextSaveGState(frameCtx);

            // 2A. Intro Overview Route
            if (f < introFrames + zoomFrames && showOverviewRoute && overviewAlpha > 0.01) {
                CGMutablePathRef fullPath = CGPathCreateMutable();
                bool started = false;
                for (size_t i = 0; i < pts.size(); i++) {
                    double sx, sy, sz;
                    if (projectPoint({pts[i].x3d, pts[i].y3d, pts[i].z3d}, sx, sy, sz)) {
                        CGFloat cgY = H - sy;
                        if (!started) {
                            CGPathMoveToPoint(fullPath, NULL, sx, cgY);
                            started = true;
                        } else {
                            CGPathAddLineToPoint(fullPath, NULL, sx, cgY);
                        }
                    }
                }
                CGContextSaveGState(frameCtx);
                CGColorRef glowCol = CGColorCreateGenericRGB(0.65, 1.0, 0.05, 0.45 * overviewAlpha);
                CGContextSetStrokeColorWithColor(frameCtx, glowCol);
                CGContextSetLineWidth(frameCtx, 9.0);
                CGContextSetLineCap(frameCtx, kCGLineCapRound);
                CGContextSetLineJoin(frameCtx, kCGLineJoinRound);
                CGContextAddPath(frameCtx, fullPath);
                CGContextStrokePath(frameCtx);
                CGColorRelease(glowCol);

                CGColorRef lineCol = CGColorCreateGenericRGB(0.75, 1.0, 0.12, 0.95 * overviewAlpha);
                CGContextSetStrokeColorWithColor(frameCtx, lineCol);
                CGContextSetLineWidth(frameCtx, 4.2);
                CGContextAddPath(frameCtx, fullPath);
                CGContextStrokePath(frameCtx);
                CGColorRelease(lineCol);
                CGContextRestoreGState(frameCtx);
                CFRelease(fullPath);
            }

            // 2B. Progressive 3D Route & Moving Activity Marker
            if (f >= introFrames) {
                CGMutablePathRef progPath = CGPathCreateMutable();
                CGMutablePathRef shadowPath = CGPathCreateMutable();
                bool started = false;
                bool shadowStarted = false;

                auto isRoutePtVisible = [&](double sx, double sy, double sz) -> bool {
                    if (sz <= 42.0 * chaseScale) return false;
                    if (sx < -200.0 || sx > W + 200.0 || sy < -200.0 || sy > H + 200.0) return false;
                    int ix = (int)std::round(sx);
                    int iy = (int)std::round(sy);
                    if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
                        float terrainDepth = zBuf[iy * W + ix];
                        if (sz > terrainDepth + 36.0f) return false;
                    }
                    return true;
                };

                for (int i = 0; i <= curIdx && i < (int)pts.size(); i++) {
                    double sx, sy, sz;
                    if (projectPoint({pts[i].x3d, pts[i].y3d, pts[i].z3d + 1.5 * chaseScale}, sx, sy, sz) && isRoutePtVisible(sx, sy, sz)) {
                        CGFloat cgY = H - sy;
                        if (!started) {
                            CGPathMoveToPoint(progPath, NULL, sx, cgY);
                            started = true;
                        } else {
                            CGPathAddLineToPoint(progPath, NULL, sx, cgY);
                        }
                    } else {
                        started = false;
                    }
                    double bsx, bsy, bsz;
                    if (projectPoint({pts[i].x3d, pts[i].y3d, std::max(0.0, pts[i].z3d - 2.5 * chaseScale)}, bsx, bsy, bsz) && isRoutePtVisible(bsx, bsy, sz)) {
                        CGFloat bcgY = H - bsy;
                        if (!shadowStarted) {
                            CGPathMoveToPoint(shadowPath, NULL, bsx, bcgY);
                            shadowStarted = true;
                        } else {
                            CGPathAddLineToPoint(shadowPath, NULL, bsx, bcgY);
                        }
                    } else {
                        shadowStarted = false;
                    }
                }
                double headSx = 0, headSy = 0, headSz = 0;
                bool headVis = projectPoint({curPt.x3d, curPt.y3d, curPt.z3d + 1.5 * chaseScale}, headSx, headSy, headSz);
                if (headVis) {
                    if (!started) CGPathMoveToPoint(progPath, NULL, headSx, H - headSy);
                    else CGPathAddLineToPoint(progPath, NULL, headSx, H - headSy);
                }
                double bHeadSx = 0, bHeadSy = 0, bHeadSz = 0;
                if (projectPoint({curPt.x3d, curPt.y3d, std::max(0.0, curPt.z3d - 2.5 * chaseScale)}, bHeadSx, bHeadSy, bHeadSz)) {
                    if (!shadowStarted) CGPathMoveToPoint(shadowPath, NULL, bHeadSx, H - bHeadSy);
                    else CGPathAddLineToPoint(shadowPath, NULL, bHeadSx, H - bHeadSy);
                }

                CGContextSaveGState(frameCtx);
                CGColorRef dropShadowCol = CGColorCreateGenericRGB(0.04, 0.08, 0.02, 0.46);
                CGContextSetStrokeColorWithColor(frameCtx, dropShadowCol);
                CGContextSetLineWidth(frameCtx, 7.5);
                CGContextSetLineCap(frameCtx, kCGLineCapRound);
                CGContextSetLineJoin(frameCtx, kCGLineJoinRound);
                CGContextAddPath(frameCtx, shadowPath);
                CGContextStrokePath(frameCtx);
                CGColorRelease(dropShadowCol);

                CGColorRef glowCol = CGColorCreateGenericRGB(0.68, 1.0, 0.08, 0.42);
                CGContextSetStrokeColorWithColor(frameCtx, glowCol);
                CGContextSetLineWidth(frameCtx, 10.5);
                CGContextAddPath(frameCtx, progPath);
                CGContextStrokePath(frameCtx);
                CGColorRelease(glowCol);

                CGColorRef coreCol = CGColorCreateGenericRGB(0.76, 1.0, 0.14, 0.98);
                CGContextSetStrokeColorWithColor(frameCtx, coreCol);
                CGContextSetLineWidth(frameCtx, 5.0);
                CGContextAddPath(frameCtx, progPath);
                CGContextStrokePath(frameCtx);
                CGColorRelease(coreCol);
                CGContextRestoreGState(frameCtx);
                CFRelease(progPath);
                CFRelease(shadowPath);

                if (headVis) {
                    CGFloat hx = headSx;
                    CGFloat hy = H - headSy;
                    int dummyIdx;
                    TrackPoint pBack  = getTrackPosAtFrac(std::max(0.0, distFrac - 0.012), dummyIdx);
                    TrackPoint pFront = getTrackPosAtFrac(std::min(1.0, distFrac + 0.012), dummyIdx);
                    double sx0, sy0, sz0, sx1, sy1, sz1;
                    CGFloat headingRad = M_PI_2;
                    if (projectPoint({pBack.x3d, pBack.y3d, pBack.z3d}, sx0, sy0, sz0) &&
                        projectPoint({pFront.x3d, pFront.y3d, pFront.z3d}, sx1, sy1, sz1)) {
                        double dxScreen = sx1 - sx0;
                        double dyScreen = (H - sy1) - (H - sy0);
                        if (dxScreen * dxScreen + dyScreen * dyScreen > 1.0) {
                            headingRad = (CGFloat)std::atan2(dyScreen, dxScreen);
                        }
                    }
                    CGFloat markerScale = isPortrait ? 1.15 : 1.10;
                    if (f >= introFrames + zoomFrames + flyFrames) {
                        double uZoom = smoothstep((double)(f - (introFrames + zoomFrames + flyFrames)) / zoomOutFrames);
                        markerScale *= (1.0 - 0.32 * uZoom);
                    }
                    drawActivityMarker(frameCtx, hx, hy, headingRad, markerScale, activeIconMode,
                                       personIconImg, bikeIconImg, carIconImg);
                }
            }

            // 2C. Overview Title Banner drawn during intro AND final pull-up overview
            if (showOverviewRoute && overviewAlpha > 0.01) {
                CGColorRef greenTitleCol = CGColorCreateGenericRGB(0.15, 0.92, 0.25, overviewAlpha);
                CGColorRef whiteTitleCol = CGColorCreateGenericRGB(1.0, 1.0, 1.0, overviewAlpha);
                if (isPortrait) {
                    drawText(frameCtx, trackName, @"PingFangSC-Semibold", 42, greenTitleCol, CGPointMake(W * 0.5, H - 175), 1);
                    NSString *statsBanner = [NSString stringWithFormat:@"%.1fKM    %.0fm", totalDist / 1000.0, totalAsc];
                    drawText(frameCtx, statsBanner, @"HelveticaNeue-Medium", 34, whiteTitleCol, CGPointMake(W * 0.5, H - 228), 1);
                } else {
                    drawText(frameCtx, trackName, @"PingFangSC-Semibold", 44, greenTitleCol, CGPointMake(W * 0.5 - 155, H - 92), 1);
                    NSString *statsBanner = [NSString stringWithFormat:@"%.1fKM    %.0fm", totalDist / 1000.0, totalAsc];
                    drawText(frameCtx, statsBanner, @"HelveticaNeue-Medium", 42, whiteTitleCol, CGPointMake(W * 0.5 + 150, H - 92), 1);
                }
                CGColorRelease(greenTitleCol);
                CGColorRelease(whiteTitleCol);
            }

            // 3. Draw HUD Overlays (without watermark)
            if (showHUD && hudAlpha > 0.01) {
                CGColorRef cardBg = CGColorCreateGenericRGB(0.18, 0.19, 0.18, 0.54 * hudAlpha);
                CGColorRef whiteCol = CGColorCreateGenericRGB(1.0, 1.0, 1.0, 0.96 * hudAlpha);
                CGColorRef labelCol = CGColorCreateGenericRGB(0.92, 0.92, 0.92, 0.85 * hudAlpha);

                CGFloat topMargin = isPortrait ? 42.0 : 20.0;
                CGFloat leftMargin = 24.0;
                CGFloat curTopY = H - topMargin;

                // Optional Average Heart Rate Card if HR data is present
                if (avgHr > 0) {
                    CGFloat hrH = 44.0;
                    CGRect hrRect = CGRectMake(leftMargin, curTopY - hrH, miniW, hrH);
                    fillRoundedRect(frameCtx, hrRect, 12.0, cardBg);
                    drawText(frameCtx, @"Average Heart Rate", @"HelveticaNeue", 13, labelCol, CGPointMake(CGRectGetMidX(hrRect), CGRectGetMaxY(hrRect) - 16), 1, true);
                    NSString *hrStr = [NSString stringWithFormat:@"%d bpm", avgHr];
                    drawText(frameCtx, hrStr, @"HelveticaNeue-Bold", 17, whiteCol, CGPointMake(CGRectGetMidX(hrRect), CGRectGetMinY(hrRect) + 7), 1, true);
                    curTopY -= (hrH + 10.0);
                }

                // 3.1 Top-Left 2D Minimap Card
                CGRect miniRect = CGRectMake(leftMargin, curTopY - miniH, miniW, miniH);
                fillRoundedRect(frameCtx, miniRect, 14.0, cardBg);

                CGContextSaveGState(frameCtx);
                CGPathRef miniClip = CGPathCreateWithRoundedRect(miniRect, 14.0, 14.0, NULL);
                CGContextAddPath(frameCtx, miniClip);
                CGContextClip(frameCtx);
                CFRelease(miniClip);
                CGContextSetAlpha(frameCtx, hudAlpha);
                CGContextDrawImage(frameCtx, miniRect, miniBgImage);

                auto mapToMini = [&](double mx, double my) -> CGPoint {
                    double u = (mx - cropX0) / boxW;
                    double v = (my - cropY0) / boxH;
                    return CGPointMake(miniRect.origin.x + u * miniRect.size.width,
                                       miniRect.origin.y + (1.0 - v) * miniRect.size.height);
                };

                CGMutablePathRef miniFull = CGPathCreateMutable();
                CGMutablePathRef miniDone = CGPathCreateMutable();
                for (size_t i = 0; i < pts.size(); i++) {
                    CGPoint mp = mapToMini(pts[i].mx, pts[i].my);
                    if (i == 0) {
                        CGPathMoveToPoint(miniFull, NULL, mp.x, mp.y);
                        CGPathMoveToPoint(miniDone, NULL, mp.x, mp.y);
                    } else {
                        CGPathAddLineToPoint(miniFull, NULL, mp.x, mp.y);
                        if ((int)i <= curIdx) {
                            CGPathAddLineToPoint(miniDone, NULL, mp.x, mp.y);
                        }
                    }
                }
                CGColorRef miniFullCol = CGColorCreateGenericRGB(0.75, 0.75, 0.75, 0.45 * hudAlpha);
                CGContextSetStrokeColorWithColor(frameCtx, miniFullCol);
                CGContextSetLineWidth(frameCtx, 2.5);
                CGContextSetLineCap(frameCtx, kCGLineCapRound);
                CGContextSetLineJoin(frameCtx, kCGLineJoinRound);
                CGContextAddPath(frameCtx, miniFull);
                CGContextStrokePath(frameCtx);
                CGColorRelease(miniFullCol);

                CGColorRef miniDoneCol = CGColorCreateGenericRGB(0.68, 1.0, 0.12, 0.95 * hudAlpha);
                CGContextSetStrokeColorWithColor(frameCtx, miniDoneCol);
                CGContextSetLineWidth(frameCtx, 3.2);
                CGContextAddPath(frameCtx, miniDone);
                CGContextStrokePath(frameCtx);
                CGColorRelease(miniDoneCol);
                CFRelease(miniFull);
                CFRelease(miniDone);

                CGPoint startMp = mapToMini(pts[0].mx, pts[0].my);
                CGColorRef redDotCol = CGColorCreateGenericRGB(0.95, 0.12, 0.12, 0.95 * hudAlpha);
                CGContextSetFillColorWithColor(frameCtx, redDotCol);
                CGContextFillEllipseInRect(frameCtx, CGRectMake(startMp.x - 4.2, startMp.y - 4.2, 8.4, 8.4));
                CGColorRelease(redDotCol);

                CGPoint curMp = mapToMini(curPt.mx, curPt.my);
                CGColorRef miniHaloCol = CGColorCreateGenericRGB(1.0, 1.0, 1.0, 0.75 * hudAlpha);
                CGColorRef miniDotCol  = CGColorCreateGenericRGB(0.76, 1.0, 0.14, 1.0 * hudAlpha);
                CGContextSetFillColorWithColor(frameCtx, miniHaloCol);
                CGContextFillEllipseInRect(frameCtx, CGRectMake(curMp.x - 8.5, curMp.y - 8.5, 17.0, 17.0));
                CGContextSetFillColorWithColor(frameCtx, miniDotCol);
                CGContextFillEllipseInRect(frameCtx, CGRectMake(curMp.x - 5.0, curMp.y - 5.0, 10.0, 10.0));
                CGColorRelease(miniHaloCol);
                CGColorRelease(miniDotCol);
                CGContextRestoreGState(frameCtx);

                curTopY -= (miniH + 14.0);

                // 3.2 Distance Card (below minimap)
                CGRect distRect = CGRectMake(leftMargin, curTopY - 115, 156, 115);
                fillRoundedRect(frameCtx, distRect, 14.0, cardBg);
                drawText(frameCtx, @"Distance", @"HelveticaNeue", 16, labelCol, CGPointMake(CGRectGetMidX(distRect), CGRectGetMaxY(distRect) - 28), 1, true);
                double km = curPt.dist / 1000.0;
                NSString *kmStr = (km < 10.0) ? [NSString stringWithFormat:@"%.2f", km] : [NSString stringWithFormat:@"%.1f", km];
                drawText(frameCtx, kmStr, @"HelveticaNeue-Bold", 26, whiteCol, CGPointMake(CGRectGetMidX(distRect), CGRectGetMidY(distRect) - 8), 1, true);
                drawText(frameCtx, @"km", @"HelveticaNeue", 16, labelCol, CGPointMake(CGRectGetMidX(distRect), CGRectGetMinY(distRect) + 18), 1, true);

                // 3.3 Bottom-Left Ascent & Descent Cards
                CGFloat botY = isPortrait ? 30.0 : 22.0;
                CGFloat cardH = isPortrait ? 92.0 : 86.0;
                CGFloat cardGap = isPortrait ? 12.0 : 10.0;
                CGRect ascRect  = CGRectMake(leftMargin, botY + cardH + cardGap, 138, cardH);
                CGRect descRect = CGRectMake(leftMargin, botY,                   138, cardH);
                fillRoundedRect(frameCtx, ascRect, 14.0, cardBg);
                fillRoundedRect(frameCtx, descRect, 14.0, cardBg);

                NSString *ascStr  = [NSString stringWithFormat:@"%.0f m", curPt.asc];
                NSString *descStr = [NSString stringWithFormat:@"%.0f m", curPt.desc];
                drawText(frameCtx, ascStr,  @"HelveticaNeue-Bold", 24, whiteCol, CGPointMake(CGRectGetMidX(ascRect),  CGRectGetMidY(ascRect) + 8), 1, true);
                drawText(frameCtx, @"Ascent", @"HelveticaNeue", 15, labelCol, CGPointMake(CGRectGetMidX(ascRect), CGRectGetMinY(ascRect) + 16), 1, true);
                drawText(frameCtx, descStr, @"HelveticaNeue-Bold", 24, whiteCol, CGPointMake(CGRectGetMidX(descRect), CGRectGetMidY(descRect) + 8), 1, true);
                drawText(frameCtx, @"Descent", @"HelveticaNeue", 15, labelCol, CGPointMake(CGRectGetMidX(descRect), CGRectGetMinY(descRect) + 16), 1, true);

                // 3.4 Bottom Elevation Profile Chart Card
                CGFloat profW = isPortrait ? (W - 176 - 24) : 910.0;
                CGFloat profH = 2 * cardH + cardGap;
                CGRect profRect = CGRectMake(176, botY, profW, profH);
                fillRoundedRect(frameCtx, profRect, 14.0, cardBg);

                CGFloat plotX0 = profRect.origin.x + 20;
                CGFloat plotX1 = CGRectGetMaxX(profRect) - 20;
                CGFloat plotY0 = profRect.origin.y + 34;
                CGFloat plotY1 = CGRectGetMaxY(profRect) - 34;
                CGFloat plotW  = plotX1 - plotX0;
                CGFloat plotH  = plotY1 - plotY0;

                auto mapEleToPlot = [&](double dist, double ele) -> CGPoint {
                    double u = (totalDist > 0) ? (dist / totalDist) : 0.0;
                    double v = (maxEle > minEle) ? ((ele - minEle) / (maxEle - minEle)) : 0.5;
                    return CGPointMake(plotX0 + u * plotW, plotY0 + v * plotH);
                };

                CGMutablePathRef fullEleArea = CGPathCreateMutable();
                CGMutablePathRef fullEleLine = CGPathCreateMutable();
                CGPathMoveToPoint(fullEleArea, NULL, plotX0, plotY0);
                for (size_t i = 0; i < pts.size(); i++) {
                    CGPoint ep = mapEleToPlot(pts[i].dist, pts[i].ele);
                    CGPathAddLineToPoint(fullEleArea, NULL, ep.x, ep.y);
                    if (i == 0) CGPathMoveToPoint(fullEleLine, NULL, ep.x, ep.y);
                    else CGPathAddLineToPoint(fullEleLine, NULL, ep.x, ep.y);
                }
                CGPathAddLineToPoint(fullEleArea, NULL, plotX1, plotY0);
                CGPathCloseSubpath(fullEleArea);

                CGColorRef unvisAreaCol = CGColorCreateGenericRGB(0.55, 0.56, 0.55, 0.30 * hudAlpha);
                CGContextSetFillColorWithColor(frameCtx, unvisAreaCol);
                CGContextAddPath(frameCtx, fullEleArea);
                CGContextFillPath(frameCtx);
                CGColorRelease(unvisAreaCol);

                CGColorRef unvisLineCol = CGColorCreateGenericRGB(0.65, 0.66, 0.65, 0.45 * hudAlpha);
                CGContextSetStrokeColorWithColor(frameCtx, unvisLineCol);
                CGContextSetLineWidth(frameCtx, 1.8);
                CGContextAddPath(frameCtx, fullEleLine);
                CGContextStrokePath(frameCtx);
                CGColorRelease(unvisLineCol);
                CFRelease(fullEleArea);
                CFRelease(fullEleLine);

                CGPoint curEp = mapEleToPlot(curPt.dist, curPt.ele);
                CGMutablePathRef doneEleArea = CGPathCreateMutable();
                CGMutablePathRef doneEleLine = CGPathCreateMutable();
                CGPathMoveToPoint(doneEleArea, NULL, plotX0, plotY0);
                for (int i = 0; i <= curIdx && i < (int)pts.size(); i++) {
                    CGPoint ep = mapEleToPlot(pts[i].dist, pts[i].ele);
                    CGPathAddLineToPoint(doneEleArea, NULL, ep.x, ep.y);
                    if (i == 0) CGPathMoveToPoint(doneEleLine, NULL, ep.x, ep.y);
                    else CGPathAddLineToPoint(doneEleLine, NULL, ep.x, ep.y);
                }
                CGPathAddLineToPoint(doneEleArea, NULL, curEp.x, curEp.y);
                CGPathAddLineToPoint(doneEleLine, NULL, curEp.x, curEp.y);
                CGPathAddLineToPoint(doneEleArea, NULL, curEp.x, plotY0);
                CGPathCloseSubpath(doneEleArea);

                CGColorRef doneAreaCol = CGColorCreateGenericRGB(0.75, 0.76, 0.75, 0.42 * hudAlpha);
                CGContextSetFillColorWithColor(frameCtx, doneAreaCol);
                CGContextAddPath(frameCtx, doneEleArea);
                CGContextFillPath(frameCtx);
                CGColorRelease(doneAreaCol);

                CGContextSetStrokeColorWithColor(frameCtx, whiteCol);
                CGContextSetLineWidth(frameCtx, 3.0);
                CGContextSetLineCap(frameCtx, kCGLineCapRound);
                CGContextSetLineJoin(frameCtx, kCGLineJoinRound);
                CGContextAddPath(frameCtx, doneEleLine);
                CGContextStrokePath(frameCtx);
                CFRelease(doneEleArea);
                CFRelease(doneEleLine);

                CGContextSetFillColorWithColor(frameCtx, whiteCol);
                CGContextFillEllipseInRect(frameCtx, CGRectMake(curEp.x - 5.5, curEp.y - 5.5, 11.0, 11.0));
                NSString *curEleStr = [NSString stringWithFormat:@"%.0fm", curPt.ele];
                drawText(frameCtx, curEleStr, @"HelveticaNeue-Bold", 14, whiteCol, CGPointMake(curEp.x, curEp.y + 12), 1, true);

                NSString *maxEleStr = [NSString stringWithFormat:@"Max %.0fm", maxEle];
                NSString *minEleStr = [NSString stringWithFormat:@"Min %.0fm", minEle];
                drawText(frameCtx, maxEleStr, @"HelveticaNeue-Bold", 15, whiteCol, CGPointMake(plotX0, CGRectGetMaxY(profRect) - 23), 0, true);
                drawText(frameCtx, minEleStr, @"HelveticaNeue-Bold", 15, whiteCol, CGPointMake(plotX0, profRect.origin.y + 10), 0, true);

                CGColorRelease(cardBg);
                CGColorRelease(whiteCol);
                CGColorRelease(labelCol);
            }

            CGContextRestoreGState(frameCtx);
        };

        if (previewOnly) {
            int testFrames[3] = {420, 780, 1660};
            NSString *tag = isPortrait ? @"9x16" : @"16x9";
            for (int idx = 0; idx < 3; idx++) {
                int tf = testFrames[idx];
                renderFrame(tf);
                CGImageRef img = CGBitmapContextCreateImage(frameCtx);
                NSString *pPath = [NSString stringWithFormat:@"%@/preview_%@_%d.jpg", previewDir, tag, tf];
                NSURL *pUrl = [NSURL fileURLWithPath:pPath];
                CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)pUrl, (__bridge CFStringRef)@"public.jpeg", 1, NULL);
                CGImageDestinationAddImage(dest, img, NULL);
                CGImageDestinationFinalize(dest);
                CFRelease(dest);
                CGImageRelease(img);
                NSLog(@"Saved %@", pPath);
            }
            if (personIconImg) CGImageRelease(personIconImg);
            if (bikeIconImg) CGImageRelease(bikeIconImg);
            if (carIconImg) CGImageRelease(carIconImg);
            CGImageRelease(miniBgImage);
            CGContextRelease(miniBgCtx);
            CGImageRelease(mosaicCGImage);
            CGContextRelease(frameCtx);
            CGContextRelease(mosaicCtx);
            CGColorSpaceRelease(colorSpace);
            return 0;
        }

        NSLog(@"Rendering %d frames at %dx%d (%d fps) -> %@...", totalFrames, W, H, fps, outVideoPath);
        for (int f = 0; f < totalFrames; f++) {
            @autoreleasepool {
                renderFrame(f);

                while (!writerInput.readyForMoreMediaData) {
                    [NSThread sleepForTimeInterval:0.002];
                }
                CVPixelBufferRef pixelBuffer = NULL;
                CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &pixelBuffer);
                if (status == kCVReturnSuccess && pixelBuffer != NULL) {
                    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
                    uint8_t *dst = (uint8_t *)CVPixelBufferGetBaseAddress(pixelBuffer);
                    size_t dstBytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
                    for (int y = 0; y < H; y++) {
                        memcpy(dst + y * dstBytesPerRow, frameBuf.data() + y * W * 4, W * 4);
                    }
                    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
                    CMTime presentTime = CMTimeMake(f, fps);
                    [adaptor appendPixelBuffer:pixelBuffer withPresentationTime:presentTime];
                    CVPixelBufferRelease(pixelBuffer);
                }
                if (f % 180 == 0 || f == totalFrames - 1) {
                    NSLog(@"Progress: %d / %d frames (%.1f%%)", f + 1, totalFrames, 100.0 * (f + 1) / totalFrames);
                }
            }
        }

        [writerInput markAsFinished];
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        NSLog(@"Successfully wrote MP4 video to %@", outVideoPath);

        if (personIconImg) CGImageRelease(personIconImg);
        if (bikeIconImg) CGImageRelease(bikeIconImg);
        if (carIconImg) CGImageRelease(carIconImg);
        CGImageRelease(miniBgImage);
        CGContextRelease(miniBgCtx);
        CGImageRelease(mosaicCGImage);
        CGContextRelease(frameCtx);
        CGContextRelease(mosaicCtx);
        CGColorSpaceRelease(colorSpace);
    }
    return 0;
}
