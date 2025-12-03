#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

// ============= ZLIB/DEFLATE DECOMPRESSION =============

class BitReader {
private:
    const std::vector<uint8_t>& data;
    size_t bytePos;
    int bitPos;

public:
    BitReader(const std::vector<uint8_t>& d) : data(d), bytePos(0), bitPos(0) {}

    uint32_t readBits(int n) {
        uint32_t result = 0;
        for (int i = 0; i < n; i++) {
            if (bytePos >= data.size()) throw std::runtime_error("Unexpected end of data");
            result |= (((data[bytePos] >> bitPos) & 1) << i);
            bitPos++;
            if (bitPos == 8) {
                bitPos = 0;
                bytePos++;
            }
        }
        return result;
    }

    void alignToByte() {
        if (bitPos != 0) {
            bitPos = 0;
            bytePos++;
        }
    }
};

class HuffmanTree {
private:
    struct Node {
        int value;
        Node* left;
        Node* right;
        Node(int v = -1) : value(v), left(nullptr), right(nullptr) {}
    };

    Node* root;

public:
    HuffmanTree() : root(nullptr) {}

    ~HuffmanTree() {
        destroyTree(root);
    }

    void buildFromLengths(const std::vector<int>& lengths) {
        destroyTree(root);
        root = new Node();

        int maxLen = 0;
        for (int len : lengths) {
            if (len > maxLen) maxLen = len;
        }
        if (maxLen == 0) return;

        std::vector<int> blCount(maxLen + 1, 0);
        for (int len : lengths) {
            if (len > 0) blCount[len]++;
        }

        std::vector<int> nextCode(maxLen + 1, 0);
        int code = 0;
        for (int bits = 1; bits <= maxLen; bits++) {
            code = (code + blCount[bits - 1]) << 1;
            nextCode[bits] = code;
        }

        for (size_t i = 0; i < lengths.size(); i++) {
            int len = lengths[i];
            if (len > 0) {
                insertCode(root, nextCode[len], len, i);
                nextCode[len]++;
            }
        }
    }

    int decode(BitReader& reader) {
        if (!root) return -1;
        Node* node = root;
        while (node && node->value == -1) {
            int bit = reader.readBits(1);
            node = (bit == 0) ? node->left : node->right;
        }
        return node ? node->value : -1;
    }

private:
    void insertCode(Node* node, int code, int len, int value) {
        for (int i = len - 1; i >= 0; i--) {
            int bit = (code >> i) & 1;
            if (bit == 0) {
                if (!node->left) node->left = new Node();
                node = node->left;
            } else {
                if (!node->right) node->right = new Node();
                node = node->right;
            }
        }
        node->value = value;
    }

    void destroyTree(Node* node) {
        if (!node) return;
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }
};

class Deflate {
public:
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed) {
        BitReader reader(compressed);
        std::vector<uint8_t> result;

        while (true) {
            int finalBlock = reader.readBits(1);
            int blockType = reader.readBits(2);

            if (blockType == 0) {
                reader.alignToByte();
                uint16_t len = reader.readBits(16);
                reader.readBits(16);
                for (int i = 0; i < len; i++) {
                    result.push_back(reader.readBits(8));
                }
            } else if (blockType == 1) {
                std::vector<int> litLenLengths(288);
                std::vector<int> distLengths(32);

                for (int i = 0; i <= 143; i++) litLenLengths[i] = 8;
                for (int i = 144; i <= 255; i++) litLenLengths[i] = 9;
                for (int i = 256; i <= 279; i++) litLenLengths[i] = 7;
                for (int i = 280; i <= 287; i++) litLenLengths[i] = 8;
                for (int i = 0; i < 32; i++) distLengths[i] = 5;

                inflateBlockData(reader, litLenLengths, distLengths, result);
            } else if (blockType == 2) {
                int hlit = reader.readBits(5) + 257;
                int hdist = reader.readBits(5) + 1;
                int hclen = reader.readBits(4) + 4;

                std::vector<int> codeLenLengths(19, 0);
                static const int codeOrder[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                for (int i = 0; i < hclen; i++) {
                    codeLenLengths[codeOrder[i]] = reader.readBits(3);
                }

                HuffmanTree codeTree;
                codeTree.buildFromLengths(codeLenLengths);

                std::vector<int> litLenLengths(hlit);
                std::vector<int> distLengths(hdist);
                int total = hlit + hdist;
                int i = 0;

                while (i < total) {
                    int code = codeTree.decode(reader);
                    if (code < 16) {
                        if (i < hlit) litLenLengths[i] = code;
                        else distLengths[i - hlit] = code;
                        i++;
                    } else if (code == 16) {
                        int count = reader.readBits(2) + 3;
                        int val = (i < hlit) ? litLenLengths[i - 1] : distLengths[i - hlit - 1];
                        for (int j = 0; j < count; j++) {
                            if (i < hlit) litLenLengths[i] = val;
                            else distLengths[i - hlit] = val;
                            i++;
                        }
                    } else if (code == 17) {
                        int count = reader.readBits(3) + 3;
                        for (int j = 0; j < count; j++) {
                            if (i < hlit) litLenLengths[i] = 0;
                            else distLengths[i - hlit] = 0;
                            i++;
                        }
                    } else if (code == 18) {
                        int count = reader.readBits(7) + 11;
                        for (int j = 0; j < count; j++) {
                            if (i < hlit) litLenLengths[i] = 0;
                            else distLengths[i - hlit] = 0;
                            i++;
                        }
                    }
                }

                inflateBlockData(reader, litLenLengths, distLengths, result);
            }

            if (finalBlock) break;
        }

        return result;
    }

private:
    static void inflateBlockData(BitReader& reader, const std::vector<int>& litLenLengths,
                                 const std::vector<int>& distLengths, std::vector<uint8_t>& result) {
        HuffmanTree litTree, distTree;
        litTree.buildFromLengths(litLenLengths);
        distTree.buildFromLengths(distLengths);

        static const int lengthExtra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const int lengthBase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const int distExtra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        static const int distBase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};

        while (true) {
            int code = litTree.decode(reader);
            if (code < 256) {
                result.push_back(code);
            } else if (code == 256) {
                break;
            } else if (code < 286) {
                int lenCode = code - 257;
                int length = lengthBase[lenCode] + reader.readBits(lengthExtra[lenCode]);
                int distCode = distTree.decode(reader);
                int distance = distBase[distCode] + reader.readBits(distExtra[distCode]);
                for (int i = 0; i < length; i++) {
                    result.push_back(result[result.size() - distance]);
                }
            }
        }
    }
};

// ============= PNG DECODER =============

struct PNGHeader {
    uint32_t width;
    uint32_t height;
    uint8_t bitDepth;
    uint8_t colorType;
    uint8_t compressionMethod;
    uint8_t filterMethod;
    uint8_t interlaceMethod;
};

class PNGDecoder {
private:
    std::vector<uint8_t> fileData;
    PNGHeader header;
    std::vector<uint8_t> imageData;
    std::vector<uint8_t> palette;

public:
    bool load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        fileData.resize(size);
        file.read(reinterpret_cast<char*>(fileData.data()), size);
        file.close();

        if (!validateSignature()) return false;
        if (!parseChunks()) return false;

        return true;
    }

    std::vector<uint8_t> getRGB() {
        std::vector<uint8_t> result;

        if (header.colorType == 0) {
            for (uint8_t val : imageData) {
                result.push_back(val);
                result.push_back(val);
                result.push_back(val);
            }
        } else if (header.colorType == 2) {
            result = imageData;
        } else if (header.colorType == 3) {
            for (uint8_t idx : imageData) {
                if (idx * 3 + 2 < palette.size()) {
                    result.push_back(palette[idx * 3]);
                    result.push_back(palette[idx * 3 + 1]);
                    result.push_back(palette[idx * 3 + 2]);
                }
            }
        } else if (header.colorType == 4) {
            for (size_t i = 0; i < imageData.size(); i += 2) {
                result.push_back(imageData[i]);
                result.push_back(imageData[i]);
                result.push_back(imageData[i]);
            }
        } else if (header.colorType == 6) {
            for (size_t i = 0; i < imageData.size(); i += 4) {
                result.push_back(imageData[i]);
                result.push_back(imageData[i + 1]);
                result.push_back(imageData[i + 2]);
            }
        }

        return result;
    }

    uint32_t getWidth() const { return header.width; }
    uint32_t getHeight() const { return header.height; }

private:
    bool validateSignature() {
        static const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (fileData.size() < 8) return false;
        for (int i = 0; i < 8; i++) {
            if (fileData[i] != sig[i]) return false;
        }
        return true;
    }

    uint32_t readBE32(size_t pos) const {
        return ((uint32_t)fileData[pos] << 24) | ((uint32_t)fileData[pos + 1] << 16) |
               ((uint32_t)fileData[pos + 2] << 8) | fileData[pos + 3];
    }

    bool parseChunks() {
        size_t pos = 8;
        std::vector<uint8_t> compressedData;

        while (pos + 12 <= fileData.size()) {
            uint32_t length = readBE32(pos);
            pos += 4;

            if (pos + 4 + length + 4 > fileData.size()) break;

            std::string type(reinterpret_cast<char*>(&fileData[pos]), 4);
            pos += 4;

            uint8_t* data = &fileData[pos];
            pos += length;
            pos += 4;  // Skip CRC

            if (type == "IHDR") {
                if (length != 13) return false;
                header.width = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                               ((uint32_t)data[2] << 8) | data[3];
                header.height = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                                ((uint32_t)data[6] << 8) | data[7];
                header.bitDepth = data[8];
                header.colorType = data[9];
                header.compressionMethod = data[10];
                header.filterMethod = data[11];
                header.interlaceMethod = data[12];
                if (header.compressionMethod != 0) return false;
                if (header.filterMethod != 0) return false;
            } else if (type == "PLTE") {
                palette.assign(data, data + length);
            } else if (type == "IDAT") {
                compressedData.insert(compressedData.end(), data, data + length);
            } else if (type == "IEND") {
                break;
            }
        }

        if (compressedData.size() < 6) return false;

        try {
            std::vector<uint8_t> deflateData(compressedData.begin() + 2, compressedData.end() - 4);
            std::vector<uint8_t> decompressed = Deflate::decompress(deflateData);
            return unfilterImageData(decompressed);
        } catch (...) {
            return false;
        }
    }

    bool unfilterImageData(const std::vector<uint8_t>& filtered) {
        uint32_t bytesPerPixel = 1;
        if (header.colorType == 2) bytesPerPixel = 3;
        else if (header.colorType == 3) bytesPerPixel = 1;
        else if (header.colorType == 4) bytesPerPixel = 2;
        else if (header.colorType == 6) bytesPerPixel = 4;

        uint32_t scanlineBytes = (header.width * bytesPerPixel * header.bitDepth + 7) / 8;
        
        size_t expectedSize = (size_t)header.height * (1 + scanlineBytes);
        if (filtered.size() < expectedSize) {
            return false;
        }
        
        imageData.clear();
        imageData.reserve(header.height * scanlineBytes);

        size_t pos = 0;
        for (uint32_t y = 0; y < header.height; y++) {
            if (pos >= filtered.size()) return false;
            uint8_t filterType = filtered[pos++];
            std::vector<uint8_t> scanline(scanlineBytes);

            for (uint32_t x = 0; x < scanlineBytes; x++) {
                if (pos >= filtered.size()) return false;
                uint8_t byte = filtered[pos++];
                uint8_t a = 0, b = 0, c = 0;

                if (x >= bytesPerPixel) {
                    a = scanline[x - bytesPerPixel];
                }
                if (y > 0) {
                    b = imageData[(y - 1) * scanlineBytes + x];
                }
                if (x >= bytesPerPixel && y > 0) {
                    c = imageData[(y - 1) * scanlineBytes + x - bytesPerPixel];
                }

                uint8_t result = byte;
                if (filterType == 1) {
                    result = byte + a;
                } else if (filterType == 2) {
                    result = byte + b;
                } else if (filterType == 3) {
                    result = byte + ((a + b) / 2);
                } else if (filterType == 4) {
                    int p = a + b - c;
                    int pa = std::abs(p - a);
                    int pb = std::abs(p - b);
                    int pc = std::abs(p - c);
                    if (pa <= pb && pa <= pc) result = byte + a;
                    else if (pb <= pc) result = byte + b;
                    else result = byte + c;
                }

                scanline[x] = result;
            }

            imageData.insert(imageData.end(), scanline.begin(), scanline.end());
        }

        return true;
    }
};

// ============= JPEG ENCODER =============

class JPEGEncoder {
private:
    std::vector<uint8_t> rgb;
    uint32_t width, height;
    int quality;
    
    // Bit buffer for entropy coding
    uint32_t bitBuf;
    int bitCount;
    std::vector<uint8_t>* outputPtr;
    
    // DC predictors for Y, Cb, Cr
    int lastDCY, lastDCCb, lastDCCr;
    
    // Quantization tables (will be scaled by quality)
    int YTable[64];
    int CbCrTable[64];
    
    // Huffman code/size tables
    uint16_t YDC_HT[12][2];   // [symbol][code, size]
    uint16_t UVDC_HT[12][2];
    uint16_t YAC_HT[256][2];
    uint16_t UVAC_HT[256][2];
    
    // Standard quantization tables
    static const uint8_t std_luminance_quant[64];
    static const uint8_t std_chrominance_quant[64];
    static const uint8_t zigzag[64];
    
    // Standard Huffman tables
    static const uint8_t std_dc_luminance_nrcodes[17];
    static const uint8_t std_dc_luminance_values[12];
    static const uint8_t std_dc_chrominance_nrcodes[17];
    static const uint8_t std_dc_chrominance_values[12];
    static const uint8_t std_ac_luminance_nrcodes[17];
    static const uint8_t std_ac_luminance_values[162];
    static const uint8_t std_ac_chrominance_nrcodes[17];
    static const uint8_t std_ac_chrominance_values[162];

public:
    JPEGEncoder(const std::vector<uint8_t>& rgb_data, uint32_t w, uint32_t h, int q)
        : rgb(rgb_data), width(w), height(h), quality(std::max(1, std::min(100, q))),
          bitBuf(0), bitCount(0), outputPtr(nullptr),
          lastDCY(0), lastDCCb(0), lastDCCr(0) {
        initQuantTables();
        initHuffmanTables();
    }

    std::vector<uint8_t> encode() {
        std::vector<uint8_t> output;
        outputPtr = &output;
        
        // SOI
        writeByte(0xFF);
        writeByte(0xD8);
        
        // APP0 (JFIF)
        writeAPP0();
        
        // DQT
        writeDQT();
        
        // SOF0
        writeSOF0();
        
        // DHT
        writeDHT();
        
        // SOS
        writeSOS();
        
        // Image data
        encodeImageData();
        
        // EOI
        writeByte(0xFF);
        writeByte(0xD9);
        
        return output;
    }

private:
    void writeByte(uint8_t b) {
        outputPtr->push_back(b);
    }
    
    void writeWord(uint16_t w) {
        writeByte((w >> 8) & 0xFF);
        writeByte(w & 0xFF);
    }
    
    void initQuantTables() {
        int q = quality;
        int scale = (q < 50) ? (5000 / q) : (200 - q * 2);
        
        for (int i = 0; i < 64; i++) {
            int yq = (std_luminance_quant[i] * scale + 50) / 100;
            int cq = (std_chrominance_quant[i] * scale + 50) / 100;
            YTable[i] = std::max(1, std::min(255, yq));
            CbCrTable[i] = std::max(1, std::min(255, cq));
        }
    }
    
    void computeHuffmanTable(const uint8_t* nrcodes, const uint8_t* values, uint16_t table[][2]) {
        uint16_t code = 0;
        int pos = 0;
        
        for (int i = 1; i <= 16; i++) {
            for (int j = 0; j < nrcodes[i]; j++) {
                table[values[pos]][0] = code;
                table[values[pos]][1] = i;
                pos++;
                code++;
            }
            code <<= 1;
        }
    }
    
    void initHuffmanTables() {
        computeHuffmanTable(std_dc_luminance_nrcodes, std_dc_luminance_values, YDC_HT);
        computeHuffmanTable(std_dc_chrominance_nrcodes, std_dc_chrominance_values, UVDC_HT);
        computeHuffmanTable(std_ac_luminance_nrcodes, std_ac_luminance_values, YAC_HT);
        computeHuffmanTable(std_ac_chrominance_nrcodes, std_ac_chrominance_values, UVAC_HT);
    }
    
    void writeAPP0() {
        writeByte(0xFF);
        writeByte(0xE0);
        writeWord(16);              // Length
        writeByte('J'); writeByte('F'); writeByte('I'); writeByte('F'); writeByte(0);
        writeByte(1); writeByte(1); // Version 1.1
        writeByte(0);               // Aspect ratio units (0 = no units)
        writeWord(1);               // X density
        writeWord(1);               // Y density
        writeByte(0);               // Thumbnail width
        writeByte(0);               // Thumbnail height
    }
    
    void writeDQT() {
        // Luminance table (written in zigzag order)
        writeByte(0xFF);
        writeByte(0xDB);
        writeWord(67);              // Length
        writeByte(0);               // Table 0, 8-bit precision
        for (int i = 0; i < 64; i++) {
            writeByte(YTable[zigzag[i]]);
        }
        
        // Chrominance table (written in zigzag order)
        writeByte(0xFF);
        writeByte(0xDB);
        writeWord(67);
        writeByte(1);               // Table 1
        for (int i = 0; i < 64; i++) {
            writeByte(CbCrTable[zigzag[i]]);
        }
    }
    
    void writeSOF0() {
        writeByte(0xFF);
        writeByte(0xC0);
        writeWord(17);              // Length
        writeByte(8);               // Precision (8 bits)
        writeWord(height);
        writeWord(width);
        writeByte(3);               // Number of components
        
        // Y component: ID=1, sampling=1x1, quant table=0
        writeByte(1);
        writeByte(0x11);
        writeByte(0);
        
        // Cb component: ID=2, sampling=1x1, quant table=1
        writeByte(2);
        writeByte(0x11);
        writeByte(1);
        
        // Cr component: ID=3, sampling=1x1, quant table=1
        writeByte(3);
        writeByte(0x11);
        writeByte(1);
    }
    
    void writeHuffmanTable(uint8_t tableClass, uint8_t tableID, 
                           const uint8_t* nrcodes, const uint8_t* values, int numValues) {
        writeByte(0xFF);
        writeByte(0xC4);
        writeWord(3 + 16 + numValues);
        writeByte((tableClass << 4) | tableID);
        for (int i = 1; i <= 16; i++) {
            writeByte(nrcodes[i]);
        }
        for (int i = 0; i < numValues; i++) {
            writeByte(values[i]);
        }
    }
    
    void writeDHT() {
        writeHuffmanTable(0, 0, std_dc_luminance_nrcodes, std_dc_luminance_values, 12);
        writeHuffmanTable(0, 1, std_dc_chrominance_nrcodes, std_dc_chrominance_values, 12);
        writeHuffmanTable(1, 0, std_ac_luminance_nrcodes, std_ac_luminance_values, 162);
        writeHuffmanTable(1, 1, std_ac_chrominance_nrcodes, std_ac_chrominance_values, 162);
    }
    
    void writeSOS() {
        writeByte(0xFF);
        writeByte(0xDA);
        writeWord(12);              // Length
        writeByte(3);               // Number of components
        
        writeByte(1);               // Y: component ID
        writeByte(0x00);            // Y: DC table 0, AC table 0
        
        writeByte(2);               // Cb: component ID
        writeByte(0x11);            // Cb: DC table 1, AC table 1
        
        writeByte(3);               // Cr: component ID
        writeByte(0x11);            // Cr: DC table 1, AC table 1
        
        writeByte(0);               // Ss (start of spectral selection)
        writeByte(63);              // Se (end of spectral selection)
        writeByte(0);               // Ah/Al
    }
    
    void writeBits(uint16_t bits, int numBits) {
        bitBuf = (bitBuf << numBits) | bits;
        bitCount += numBits;
        
        while (bitCount >= 8) {
            uint8_t b = (bitBuf >> (bitCount - 8)) & 0xFF;
            writeByte(b);
            if (b == 0xFF) {
                writeByte(0x00);    // Byte stuffing
            }
            bitCount -= 8;
        }
    }
    
    void flushBits() {
        if (bitCount > 0) {
            uint8_t b = (bitBuf << (8 - bitCount)) & 0xFF;
            writeByte(b);
            if (b == 0xFF) {
                writeByte(0x00);
            }
        }
        bitBuf = 0;
        bitCount = 0;
    }
    
    int calcBitSize(int value) {
        int absVal = (value < 0) ? -value : value;
        int bits = 0;
        while (absVal > 0) {
            bits++;
            absVal >>= 1;
        }
        return bits;
    }
    
    void encodeDC(int dc, uint16_t dcTable[][2]) {
        int bits = calcBitSize(dc);
        
        // Write Huffman code for the category (number of bits needed)
        writeBits(dcTable[bits][0], dcTable[bits][1]);
        
        // Write the actual value
        if (bits > 0) {
            int val = dc;
            if (dc < 0) {
                val = dc - 1;       // Convert to one's complement for negative
            }
            writeBits(val & ((1 << bits) - 1), bits);
        }
    }
    
    void encodeAC(int* block, uint16_t acTable[][2]) {
        int zeroCount = 0;
        
        for (int i = 1; i < 64; i++) {
            if (block[i] == 0) {
                zeroCount++;
            } else {
                // Handle runs of more than 15 zeros
                while (zeroCount >= 16) {
                    writeBits(acTable[0xF0][0], acTable[0xF0][1]);  // ZRL (16 zeros)
                    zeroCount -= 16;
                }
                
                int bits = calcBitSize(block[i]);
                int symbol = (zeroCount << 4) | bits;
                
                writeBits(acTable[symbol][0], acTable[symbol][1]);
                
                int val = block[i];
                if (val < 0) {
                    val = block[i] - 1;
                }
                writeBits(val & ((1 << bits) - 1), bits);
                
                zeroCount = 0;
            }
        }
        
        // End of block
        if (zeroCount > 0) {
            writeBits(acTable[0][0], acTable[0][1]);  // EOB
        }
    }
    
    void forwardDCT(float* block) {
        // AAN (Arai, Agui, Nakajima) fast DCT algorithm
        const float c1 = 0.980785280f;  // cos(1*pi/16)
        const float c2 = 0.923879533f;  // cos(2*pi/16) 
        const float c3 = 0.831469612f;  // cos(3*pi/16)
        const float c4 = 0.707106781f;  // cos(4*pi/16) = 1/sqrt(2)
        const float c5 = 0.555570233f;  // cos(5*pi/16)
        const float c6 = 0.382683432f;  // cos(6*pi/16)
        const float c7 = 0.195090322f;  // cos(7*pi/16)
        
        // Process rows
        for (int i = 0; i < 8; i++) {
            float* row = block + i * 8;
            
            float tmp0 = row[0] + row[7];
            float tmp7 = row[0] - row[7];
            float tmp1 = row[1] + row[6];
            float tmp6 = row[1] - row[6];
            float tmp2 = row[2] + row[5];
            float tmp5 = row[2] - row[5];
            float tmp3 = row[3] + row[4];
            float tmp4 = row[3] - row[4];
            
            float tmp10 = tmp0 + tmp3;
            float tmp13 = tmp0 - tmp3;
            float tmp11 = tmp1 + tmp2;
            float tmp12 = tmp1 - tmp2;
            
            row[0] = tmp10 + tmp11;
            row[4] = tmp10 - tmp11;
            
            float z1 = (tmp12 + tmp13) * c4;
            row[2] = tmp13 + z1;
            row[6] = tmp13 - z1;
            
            tmp10 = tmp4 + tmp5;
            tmp11 = tmp5 + tmp6;
            tmp12 = tmp6 + tmp7;
            
            float z5 = (tmp10 - tmp12) * c6;
            float z2 = tmp10 * c2 + z5;
            float z4 = tmp12 * c6 + z5;
            float z3 = tmp11 * c4;
            
            float z11 = tmp7 + z3;
            float z13 = tmp7 - z3;
            
            row[5] = z13 + z2;
            row[3] = z13 - z2;
            row[1] = z11 + z4;
            row[7] = z11 - z4;
        }
        
        // Process columns
        for (int i = 0; i < 8; i++) {
            float tmp0 = block[0*8+i] + block[7*8+i];
            float tmp7 = block[0*8+i] - block[7*8+i];
            float tmp1 = block[1*8+i] + block[6*8+i];
            float tmp6 = block[1*8+i] - block[6*8+i];
            float tmp2 = block[2*8+i] + block[5*8+i];
            float tmp5 = block[2*8+i] - block[5*8+i];
            float tmp3 = block[3*8+i] + block[4*8+i];
            float tmp4 = block[3*8+i] - block[4*8+i];
            
            float tmp10 = tmp0 + tmp3;
            float tmp13 = tmp0 - tmp3;
            float tmp11 = tmp1 + tmp2;
            float tmp12 = tmp1 - tmp2;
            
            block[0*8+i] = tmp10 + tmp11;
            block[4*8+i] = tmp10 - tmp11;
            
            float z1 = (tmp12 + tmp13) * c4;
            block[2*8+i] = tmp13 + z1;
            block[6*8+i] = tmp13 - z1;
            
            tmp10 = tmp4 + tmp5;
            tmp11 = tmp5 + tmp6;
            tmp12 = tmp6 + tmp7;
            
            float z5 = (tmp10 - tmp12) * c6;
            float z2 = tmp10 * c2 + z5;
            float z4 = tmp12 * c6 + z5;
            float z3 = tmp11 * c4;
            
            float z11 = tmp7 + z3;
            float z13 = tmp7 - z3;
            
            block[5*8+i] = z13 + z2;
            block[3*8+i] = z13 - z2;
            block[1*8+i] = z11 + z4;
            block[7*8+i] = z11 - z4;
        }
    }
    
    void processBlock(float* block, int* quantTable, int& lastDC, 
                      uint16_t dcTable[][2], uint16_t acTable[][2]) {
        // Forward DCT
        forwardDCT(block);
        
        // Quantize and reorder to zigzag sequence for encoding
        // zigzag[i] gives the natural (row-major) index for zigzag position i
        int quantized[64];
        for (int i = 0; i < 64; i++) {
            int naturalIdx = zigzag[i];
            float val = block[naturalIdx] / (quantTable[naturalIdx] * 8.0f);
            quantized[i] = (int)((val > 0) ? (val + 0.5f) : (val - 0.5f));
        }
        
        // Encode DC coefficient
        int dcVal = quantized[0] - lastDC;
        lastDC = quantized[0];
        encodeDC(dcVal, dcTable);
        
        // Encode AC coefficients
        encodeAC(quantized, acTable);
    }
    
    void encodeImageData() {
        lastDCY = lastDCCb = lastDCCr = 0;
        
        // Process image in 8x8 blocks
        for (uint32_t y = 0; y < height; y += 8) {
            for (uint32_t x = 0; x < width; x += 8) {
                float blockY[64], blockCb[64], blockCr[64];
                
                // Extract and convert RGB to YCbCr
                for (int by = 0; by < 8; by++) {
                    for (int bx = 0; bx < 8; bx++) {
                        uint32_t py = std::min(y + by, height - 1);
                        uint32_t px = std::min(x + bx, width - 1);
                        size_t idx = (py * width + px) * 3;
                        
                        float r = rgb[idx];
                        float g = rgb[idx + 1];
                        float b = rgb[idx + 2];
                        
                        // RGB to YCbCr conversion (level shifted by -128)
                        blockY[by * 8 + bx]  =  0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                        blockCb[by * 8 + bx] = -0.168736f * r - 0.331264f * g + 0.5f * b;
                        blockCr[by * 8 + bx] =  0.5f * r - 0.418688f * g - 0.081312f * b;
                    }
                }
                
                // Process Y block
                processBlock(blockY, YTable, lastDCY, YDC_HT, YAC_HT);
                
                // Process Cb block
                processBlock(blockCb, CbCrTable, lastDCCb, UVDC_HT, UVAC_HT);
                
                // Process Cr block
                processBlock(blockCr, CbCrTable, lastDCCr, UVDC_HT, UVAC_HT);
            }
        }
        
        flushBits();
    }
};

// Standard luminance quantization table
const uint8_t JPEGEncoder::std_luminance_quant[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};

// Standard chrominance quantization table
const uint8_t JPEGEncoder::std_chrominance_quant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

// Zigzag order
const uint8_t JPEGEncoder::zigzag[64] = {
    0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// DC luminance Huffman table
const uint8_t JPEGEncoder::std_dc_luminance_nrcodes[17] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
const uint8_t JPEGEncoder::std_dc_luminance_values[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

// DC chrominance Huffman table
const uint8_t JPEGEncoder::std_dc_chrominance_nrcodes[17] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
const uint8_t JPEGEncoder::std_dc_chrominance_values[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

// AC luminance Huffman table
const uint8_t JPEGEncoder::std_ac_luminance_nrcodes[17] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
const uint8_t JPEGEncoder::std_ac_luminance_values[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

// AC chrominance Huffman table
const uint8_t JPEGEncoder::std_ac_chrominance_nrcodes[17] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
const uint8_t JPEGEncoder::std_ac_chrominance_values[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
    0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
    0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

// ============= MAIN CONVERTER =============

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.png> <output.jpg> [quality 1-100]\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    int quality = 85;

    if (argc > 3) {
        quality = std::atoi(argv[3]);
    }

    std::cout << "Loading PNG: " << inputFile << std::endl;

    PNGDecoder decoder;
    if (!decoder.load(inputFile)) {
        std::cerr << "Failed to load PNG file\n";
        return 1;
    }

    std::cout << "PNG loaded: " << decoder.getWidth() << "x" << decoder.getHeight() << std::endl;

    std::vector<uint8_t> rgb = decoder.getRGB();
    if (rgb.empty()) {
        std::cerr << "Failed to extract RGB data\n";
        return 1;
    }

    std::cout << "Encoding JPEG with quality " << quality << "..." << std::endl;

    JPEGEncoder encoder(rgb, decoder.getWidth(), decoder.getHeight(), quality);
    std::vector<uint8_t> jpegData = encoder.encode();

    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }

    outFile.write(reinterpret_cast<const char*>(jpegData.data()), jpegData.size());
    outFile.close();

    std::cout << "Successfully converted to: " << outputFile << std::endl;
    std::cout << "File size: " << jpegData.size() << " bytes" << std::endl;

    return 0;
}
