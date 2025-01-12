#include "pch.h"
#include "score.h"

#include "EmbeddedData.h"
#include "fullscrn.h"
#include "loader.h"
#include "GroupData.h"
#include "pb.h"
#include "render.h"


score_msg_font_type* score::msg_fontp;

int score::init()
{
	return 1;
}

scoreStruct* score::create(LPCSTR fieldName, gdrv_bitmap8* renderBgBmp)
{
	auto score = new scoreStruct();
	if (!score)
		return nullptr;
	score->Score = -9999;
	score->BackgroundBmp = renderBgBmp;

	/*Full tilt: score box dimensions index is offset by resolution*/
	auto dimensionsId = pb::record_table->record_labeled(fieldName) + fullscrn::GetResolution();
	auto dimensions = reinterpret_cast<int16_t*>(loader::loader_table->field(dimensionsId,
	                                                                         FieldTypes::ShortArray));
	if (!dimensions)
	{
		delete score;
		return nullptr;
	}
	int groupIndex = *dimensions++;
	score->OffsetX = *dimensions++;
	score->OffsetY = *dimensions++;
	score->Width = *dimensions++;
	score->Height = *dimensions;

	for (int index = 0; index < 10; index++)
	{
		score->CharBmp[index] = loader::loader_table->GetBitmap(groupIndex);
		++groupIndex;
	}
	return score;
}

scoreStruct* score::dup(scoreStruct* score, int scoreIndex)
{
	return new scoreStruct(*score);
}

void score::load_msg_font(LPCSTR lpName)
{
	/*3DPB stores font in resources, FT in dat. FT font has multiple resolutions*/
	if (pb::FullTiltMode)
		load_msg_font_FT(lpName);
	else
		load_msg_font_3DPB(lpName);
}

void score::load_msg_font_3DPB(LPCSTR lpName)
{
	if (strcmp(lpName, "pbmsg_ft") != 0)
		return;

	auto rcData = reinterpret_cast<int16_t*>(ImFontAtlas::DecompressCompressedBase85Data(
		EmbeddedData::PB_MSGFT_bin_compressed_data_base85));

	auto fontp = new score_msg_font_type();
	msg_fontp = fontp;
	if (!fontp)
	{
		return;
	}
	memset(fontp->Chars, 0, sizeof fontp->Chars);

	auto maxWidth = 0;
	auto ptrToWidths = (char*)rcData + 6;
	for (auto index = 128; index; index--)
	{
		if (*ptrToWidths > maxWidth)
			maxWidth = *ptrToWidths;
		++ptrToWidths;
	}

	auto height = rcData[2];
	auto tmpCharBur = new char[maxWidth * height + 4];
	if (!tmpCharBur)
	{
		delete msg_fontp;
		msg_fontp = nullptr;
		IM_FREE(rcData);
		return;
	}

	msg_fontp->GapWidth = rcData[0];
	msg_fontp->Height = height;

	auto ptrToData = (char*)(rcData + 67);
	int charInd;
	for (charInd = 0; charInd < 128; charInd++)
	{
		auto width = *((char*)rcData + 6 + charInd);
		if (!width)
			continue;

		auto bmp = new gdrv_bitmap8(width, height, true);
		msg_fontp->Chars[charInd] = bmp;

		auto sizeInBytes = height * width + 1;
		memcpy(tmpCharBur + 3, ptrToData, sizeInBytes);
		ptrToData += sizeInBytes;

		auto srcPtr = tmpCharBur + 4;
		auto dstPtr = &bmp->IndexedBmpPtr[bmp->Stride * (bmp->Height - 1)];
		for (auto y = 0; y < height; ++y)
		{
			memcpy(dstPtr, srcPtr, width);
			srcPtr += width;
			dstPtr -= bmp->Stride;
		}
	}

	delete[] tmpCharBur;
	IM_FREE(rcData);
	if (charInd != 128)
		unload_msg_font();
}

void score::load_msg_font_FT(LPCSTR lpName)
{
	if (!pb::record_table)
		return;
	int groupIndex = pb::record_table->record_labeled(lpName);
	if (groupIndex < 0)
		return;
	msg_fontp = new score_msg_font_type();
	if (!msg_fontp)
		return;

	memset(msg_fontp, 0, sizeof(score_msg_font_type));
	auto gapArray = reinterpret_cast<int16_t*>(pb::record_table->field(groupIndex, FieldTypes::ShortArray));
	if (gapArray)
		msg_fontp->GapWidth = gapArray[fullscrn::GetResolution()];
	else
		msg_fontp->GapWidth = 0;
	for (auto charIndex = 32; charIndex < 128; charIndex++, ++groupIndex)
	{
		auto bmp = pb::record_table->GetBitmap(groupIndex);
		if (!bmp)
			break;
		if (!msg_fontp->Height)
			msg_fontp->Height = bmp->Height;
		msg_fontp->Chars[charIndex] = bmp;
	}
}

void score::unload_msg_font()
{
	if (msg_fontp)
	{
		/*3DBP creates bitmaps, FT just references them from partman*/
		if (!pb::FullTiltMode)
			for (auto& Char : msg_fontp->Chars)
			{
				delete Char;
			}
		delete msg_fontp;
		msg_fontp = nullptr;
	}
}

void score::erase(scoreStruct* score, int blitFlag)
{
	if (score)
	{
		if (score->BackgroundBmp)
			gdrv::copy_bitmap(
				render::vscreen,
				score->Width,
				score->Height,
				score->OffsetX,
				score->OffsetY,
				score->BackgroundBmp,
				score->OffsetX,
				score->OffsetY);
		else
			gdrv::fill_bitmap(render::vscreen, score->Width, score->Height, score->OffsetX, score->OffsetY, 0);
	}
}

void score::set(scoreStruct* score, int value)
{
	if (score)
	{
		score->Score = value;
		score->DirtyFlag = true;
	}
}


void score::update(scoreStruct* score)
{
	char scoreBuf[12]{};
	if (score && score->DirtyFlag && score->Score <= 1000000000)
	{
		score->DirtyFlag = false;
		int x = score->Width + score->OffsetX;
		int y = score->OffsetY;
		erase(score, 0);
		if (score->Score >= 0)
		{
			snprintf(scoreBuf, sizeof scoreBuf, "%d", score->Score);
			for (ptrdiff_t index = strlen(scoreBuf) - 1; index >= 0; index--)
			{
				unsigned char curChar = scoreBuf[index];
				curChar -= '0';
				gdrv_bitmap8* bmp = score->CharBmp[curChar % 10u];
				x -= bmp->Width;
				int height = bmp->Height;
				int width = bmp->Width;
				if (render::background_bitmap)
					gdrv::copy_bitmap_w_transparency(render::vscreen, width, height, x, y, bmp, 0, 0);
				else
					gdrv::copy_bitmap(render::vscreen, width, height, x, y, bmp, 0, 0);
			}
		}
	}
}

void score::string_format(int score, char* str)
{
	if (score == -999)
	{
		*str = 0;
	}
	else
	{
		int scoreMillions = score % 1000000000 / 1000000;
		if (score / 1000000000 <= 0)
		{
			if (scoreMillions <= 0)
			{
				if (score % 1000000 / 1000 <= 0)
					snprintf(str, 36, "%d", score);
				else
					snprintf(str, 36, "%d,%03d", score % 1000000 / 1000, score % 1000);
			}
			else
			{
				snprintf(str, 36, "%d,%03d,%03d", scoreMillions, score % 1000000 / 1000, score % 1000);
			}
		}
		else
		{
			snprintf(
				str,
				36,
				"%d,%03d,%03d,%03d",
				score / 1000000000,
				scoreMillions,
				score % 1000000 / 1000,
				score % 1000);
		}
	}
}

void score::ApplyPalette()
{
	if (!msg_fontp || pb::FullTiltMode)
		return;

	// Only 3DPB font needs this, because it is not loaded by partman
	for (auto& Char : msg_fontp->Chars)
	{
		if (Char)
		{
			gdrv::ApplyPalette(*Char);
		}
	}
}
