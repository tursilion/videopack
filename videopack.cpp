// This program takes a folder full of TI bitmap images
// and a raw audio file in TI format (ie: with the sound
// control nibble prepended), and packs a single binary
// blob.
//
// the extra sound bytes take into account VDP set up time for the different
// copy loops, to keep the sound output at a steady rate.

#include "stdafx.h"
#include <Windows.h>
#include <atlstr.h>
#include <stdio.h>

// when true, monochrome video and no color table
bool MONO = false;

// storage for TI bitmap data
char workbuf1[6144];
char workbuf2[6144];
char oldp[6144];
char oldc[6144];

int main(int argc, char* argv[])
{
    printf("videopack 20220720\n");

	if (argc < 4) {
		printf("videopack <ti bitmap prefix (5 digits appended)> <audio bin> <output file> [-mono]\n");
		return -1;
	}

	FILE *fout = fopen(argv[3], "wb");
	if (NULL == fout) {
		printf("can't open output file\n");
		return -1;
	}
	FILE *faudio = fopen(argv[2], "rb");
	if (NULL == faudio) {
		printf("can't open audio file\n");
		return -1;
	}

    if (argc > 4) {
        if (0 == strcmp(argv[4],"-mono")) {
            MONO = true;
            printf("Doing monochrome video...\n");
        }
    }

	CString path = argv[1];

	printf("\n");
	memset(oldp, 0, sizeof(oldp));
	memset(oldc, 0, sizeof(oldc));	// zero out the old frame

	// max of 100,000 frames, although that's a limitation of THIS tool, nothing else
	int frames = 0;
	for (int cnt = 0; cnt<100000; cnt++) {
		CString test;
		test.Format("%s%05d.tiap", path.GetString(), cnt);
		FILE *fp = fopen(test.GetString(), "rb");
		if (NULL == fp) continue;
		++frames;

		printf("\r%s", test.GetString());
		// found a file - the color image is expected to also be present
		if (6144 != fread(workbuf1, 1, 6144, fp)) {
			printf("\nBad read on picture file %s\n", test.GetString());
			return -1;
		}
		fclose(fp);

        if (!MONO) {
		    test=test.Left(test.GetLength()-1);
		    test+='c';
		    fp = fopen(test.GetString(), "rb");
		    if (NULL == fp) {
			    printf("\nCan't open color file %s\n", test.GetString());
			    return -1;
		    }
		    // found a file - the color image is expected to also be present
		    if (6144 != fread(workbuf2, 1, 6144, fp)) {
			    printf("\nBad read on color file %s\n", test.GetString());
			    return -1;
		    }
		    fclose(fp);

    #if 0
		    // This looks worse!
            // TODO: but isn't this what the despeckle tool does??

		    // first fixup phase - we check colors against the previous frame. The converter
		    // always tries to put the most populous color in the foreground. In some cases
		    // (especially black and white), movement could cause the color palette to swap
		    // unnecessarily, causing bigger pixel jumps than needed. So, all we are doing
		    // here is checking - if a color palette matches the previous frame's palette,
		    // swap it so that the colors don't change if they don't need to. This should
		    // reduce visible artifacts in those cases.
		    for (int idx=0; idx<6144; idx++) {
			    bool swapneeded = false;
			    // if foreground color matches, we don't need to check it any further
			    if ((workbuf2[idx] & 0xf0) != (oldc[idx]&0xf0)) {
				    // does not match - does it match the background color?
				    if ((workbuf2[idx] & 0xf0) == ((oldc[idx]&0x0f)<<4)) {
					    // yes, do the swap
					    swapneeded = true;
				    }
    #if 0
				    // looks awful with background swaps
				    else {
					    // check background (only if foreground didn't already match)
					    if ((workbuf2[idx]&0x0f) != (oldc[idx]&0x0f)) {
						    // does not match, does it match foreground?
						    if ((workbuf2[idx]&0x0f) == ((oldc[idx]&0xf0)>>4)) {
							    // yes, do the swap
							    swapneeded = true;
						    }
					    }
				    }
    #endif
			    }
			    if (swapneeded) {
				    // swap colors and pixels to better match the previous frame
				    workbuf2[idx] = ((workbuf2[idx]&0xf0)>>4) | ((workbuf2[idx]&0x0f)<<4);
				    workbuf1[idx] = ~workbuf1[idx];
			    }
		    }
    #endif

		    // but this one helps a lot
		    // second quick fixup phase -- the image converter has a habit of using black
		    // for unused colors (ie: solid pattern) - this causes black sparkles in animation
		    // So, check and fixup - if we find a pattern byte that is either 0xff or 0x00,
		    // set it to 0x00 (no pixels set) and set the color foreground and background
		    // to be the same. When the new pixels are set, nothing should change till the color
		    // table is updated, which is second, reducing sparkles.
		    // TODO: we could do one better and use lookahead for any solid pattern to pre-
		    // select the unused color to something useful - but I guess not really useful, it
		    // won't visibly change till the color is updated anyway.
		    for (int idx=0; idx<6144; idx++) {
			    if (workbuf1[idx] == 0) {
				    // just a background color, so fixup the color table
				    workbuf2[idx] = workbuf2[idx] & 0x0f;
				    workbuf2[idx] |= workbuf2[idx]<<4;
			    } else if (workbuf1[idx] == 0xff) {
				    // just a foreground color - make background and fixup
				    workbuf1[idx] = 0;
				    workbuf2[idx] = (workbuf2[idx] & 0xf0)>>4;
				    workbuf2[idx] |= workbuf2[idx]<<4;
			    }
		    }

		    // save the results
		    memcpy(oldp, workbuf1, sizeof(oldp));
		    memcpy(oldc, workbuf2, sizeof(oldc));
        }   // !mono

		// we want to read the first 16 rows, and the first 24 characters of each row
		// files 1 and 2 pretty much track, but it's easier with separate counters
		int filepos1 = 0;
		int filepos2 = 0;

		// position in the video data where we need to skip over 64 bytes
		// it's always a multiple of 8, so we can safely check every 4
		int jumppos1 = 192;	
		int jumppos2 = 192;

		// skip TIFILES header (assume no images ACTUALLY start with that!)
		if (0 == memcmp(workbuf1, "\x7TIFILES", 8)) {
			filepos1+=128;
			jumppos1+=128;
		}
		if (0 == memcmp(workbuf2, "\x7TIFILES", 8)) {
			filepos2+=128;
			jumppos2+=128;
		}

		//* The data pattern needs to be (P=PATTERN, C=COLOR, S=Sound):
		//* 1		S
		//* 192	PPPPS	first 1/3rd, chars 0-95
		//* 1		S
		//* 192	CCCCS
		//* 1		S
		//* 192	PPPPS	second 1/3rd, chars 0-95
		//* 1		S
		//* 192	CCCCS
		//* 1		S
		//* 192	PPPPS	second 1/3rd, chars 96-191
		//* 1		S
		//* 192	CCCCS
		//* 1		S
		//* 192	PPPPS	third 1/3rd, chars 0-95
		//* 1		S
		//* 192	CCCCS
		//
		// Each video frame thus has 6144 bytes of video data (3072 each pattern and color)
		// and 1544 bytes of audio data.

		// first third
		fputc(fgetc(faudio), fout);		// audio byte

		if (feof(faudio)) {
			printf("\nAudio ended early. (loop)\n");
			fseek(faudio, 0, SEEK_SET);
		}

		for (int idx=0; idx<192; idx++) {
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(fgetc(faudio), fout);		// audio byte

			// every 192 bytes in the video file, we need to jump to the next line (24 columns)
			// That's an addition of 8 columns, or 64 bytes.
			if (filepos1 == jumppos1) {
				filepos1+=64;
				jumppos1=filepos1+192;
			}
		}

        if (!MONO) {
		    fputc(fgetc(faudio), fout);		// audio byte

		    for (int idx=0; idx<192; idx++) {
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(fgetc(faudio), fout);		// audio byte

			    // every 192 bytes in the video file, we need to jump to the next line (24 columns)
			    // That's an addition of 8 columns, or 64 bytes.
			    if (filepos2 == jumppos2) {
				    filepos2+=64;
				    jumppos2=filepos2+192;
			    }
		    }
        }

		// second third part 1
		fputc(fgetc(faudio), fout);		// audio byte

		for (int idx=0; idx<192; idx++) {
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(fgetc(faudio), fout);		// audio byte

			// every 192 bytes in the video file, we need to jump to the next line (24 columns)
			// That's an addition of 8 columns, or 64 bytes.
			if (filepos1 == jumppos1) {
				filepos1+=64;
				jumppos1=filepos1+192;
			}
		}

        if (!MONO) {
		    fputc(fgetc(faudio), fout);		// audio byte

		    for (int idx=0; idx<192; idx++) {
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(fgetc(faudio), fout);		// audio byte

			    // every 192 bytes in the video file, we need to jump to the next line (24 columns)
			    // That's an addition of 8 columns, or 64 bytes.
			    if (filepos2 == jumppos2) {
				    filepos2+=64;
				    jumppos2=filepos2+192;
			    }
		    }
        }

		// second third part 2
		fputc(fgetc(faudio), fout);		// audio byte

		for (int idx=0; idx<192; idx++) {
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(fgetc(faudio), fout);		// audio byte

			// every 192 bytes in the video file, we need to jump to the next line (24 columns)
			// That's an addition of 8 columns, or 64 bytes.
			if (filepos1 == jumppos1) {
				filepos1+=64;
				jumppos1=filepos1+192;
			}
		}

        if (!MONO) {
		    fputc(fgetc(faudio), fout);		// audio byte

		    for (int idx=0; idx<192; idx++) {
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(fgetc(faudio), fout);		// audio byte

			    // every 192 bytes in the video file, we need to jump to the next line (24 columns)
			    // That's an addition of 8 columns, or 64 bytes.
			    if (filepos2 == jumppos2) {
				    filepos2+=64;
				    jumppos2=filepos2+192;
			    }
		    }
        }

		// third third
		fputc(fgetc(faudio), fout);		// audio byte

		for (int idx=0; idx<192; idx++) {
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(workbuf1[filepos1++], fout);
			fputc(fgetc(faudio), fout);		// audio byte

			// every 192 bytes in the video file, we need to jump to the next line (24 columns)
			// That's an addition of 8 columns, or 64 bytes.
			if (filepos1 == jumppos1) {
				filepos1+=64;
				jumppos1=filepos1+192;
			}
		}

        if (!MONO) {
		    fputc(fgetc(faudio), fout);		// audio byte

		    for (int idx=0; idx<192; idx++) {
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(workbuf2[filepos2++], fout);
			    fputc(fgetc(faudio), fout);		// audio byte

			    // every 192 bytes in the video file, we need to jump to the next line (24 columns)
			    // That's an addition of 8 columns, or 64 bytes.
			    if (filepos2 == jumppos2) {
				    filepos2+=64;
				    jumppos2=filepos2+192;
			    }
		    }
        }

	}

	// so we finished, how'd the audio end up?
	int pos = ftell(faudio);
	fseek(faudio, 0, SEEK_END);
	printf("\n%d frames, Audio ended with %d bytes left\n", frames, ftell(faudio)-pos);

	fclose(fout);
	fclose(faudio);

	return 0;
}

