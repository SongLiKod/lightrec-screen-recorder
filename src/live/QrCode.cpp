#include "live/QrCode.h"

#include <QPainter>
#include <QVector>

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace lightrec {

namespace {

// ---------------------------------------------------------------------------
// QR tables (error correction level M, versions 1..10)
// ---------------------------------------------------------------------------
const int kTotalCodewords[] = { 26, 44, 70, 100, 134, 172, 196, 242, 292, 346 };
const int kEccCodewords[] = { 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
const int kBlocks[] = { 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 };
const int kAlignment[][4] = {
    { 0, 0, 0, 0 },
    { 6, 18, 0, 0 },
    { 6, 22, 0, 0 },
    { 6, 26, 0, 0 },
    { 6, 30, 0, 0 },
    { 6, 34, 0, 0 },
    { 6, 22, 38, 0 },
    { 6, 24, 42, 0 },
    { 6, 26, 46, 0 },
    { 6, 28, 50, 0 }
};

class BitBuffer
{
public:
    void appendBits(unsigned int value, int length)
    {
        for (int i = length - 1; i >= 0; --i)
            m_bits.push_back((value >> i) & 1u);
    }
    void appendBytes(const QByteArray &data)
    {
        for (unsigned char byte : data) {
            for (int i = 7; i >= 0; --i)
                m_bits.push_back((byte >> i) & 1u);
        }
    }
    int size() const { return static_cast<int>(m_bits.size()); }
    bool bit(int index) const { return m_bits[static_cast<size_t>(index)] != 0; }
    std::vector<unsigned char> toCodewords() const
    {
        std::vector<unsigned char> result((m_bits.size() + 7) / 8, 0);
        for (size_t i = 0; i < m_bits.size(); ++i)
            result[i / 8] |= static_cast<unsigned char>(m_bits[i] << (7 - (i % 8)));
        return result;
    }

private:
    std::vector<unsigned char> m_bits;
};

class ReedSolomon
{
public:
    static std::vector<unsigned char> multiply(const std::vector<unsigned char> &left,
                                               const std::vector<unsigned char> &right)
    {
        std::vector<unsigned char> result(left.size() + right.size() - 1, 0);
        for (size_t i = 0; i < left.size(); ++i) {
            for (size_t j = 0; j < right.size(); ++j)
                result[i + j] ^= gfMultiply(left[i], right[j]);
        }
        return result;
    }

    static std::vector<unsigned char> divisor(int degree)
    {
        std::vector<unsigned char> result(static_cast<size_t>(degree), 0);
        result[static_cast<size_t>(degree - 1)] = 1;
        unsigned char root = 1;
        for (int i = 0; i < degree; ++i) {
            for (int j = 0; j < degree; ++j) {
                result[static_cast<size_t>(j)] = gfMultiply(result[static_cast<size_t>(j)], root);
                if (j + 1 < degree)
                    result[static_cast<size_t>(j)] ^= result[static_cast<size_t>(j + 1)];
            }
            root = gfMultiply(root, 2);
        }
        return result;
    }

    static std::vector<unsigned char> remainder(const std::vector<unsigned char> &data,
                                                const std::vector<unsigned char> &divisor)
    {
        std::vector<unsigned char> result(divisor.size(), 0);
        for (unsigned char byte : data) {
            const unsigned char factor = static_cast<unsigned char>(byte ^ result.front());
            result.erase(result.begin());
            result.push_back(0);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] ^= gfMultiply(divisor[i], factor);
        }
        return result;
    }

private:
    static unsigned char gfMultiply(unsigned char x, unsigned char y)
    {
        int z = 0;
        for (int i = 7; i >= 0; --i) {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        return static_cast<unsigned char>(z);
    }
};

struct Matrix {
    int size = 0;
    std::vector<unsigned char> modules;
    std::vector<unsigned char> function;

    explicit Matrix(int dimension = 0)
        : size(dimension)
        , modules(static_cast<size_t>(dimension * dimension), 0)
        , function(static_cast<size_t>(dimension * dimension), 0)
    {
    }

    unsigned char &at(int x, int y) { return modules[static_cast<size_t>(y * size + x)]; }
    unsigned char at(int x, int y) const { return modules[static_cast<size_t>(y * size + x)]; }
    bool isFunction(int x, int y) const { return function[static_cast<size_t>(y * size + x)] != 0; }
    void setFunction(int x, int y, bool value)
    {
        if (x < 0 || y < 0 || x >= size || y >= size)
            return;
        at(x, y) = value ? 1 : 0;
        function[static_cast<size_t>(y * size + x)] = 1;
    }
};

int charCountBits(int version)
{
    return version <= 9 ? 8 : 16;
}

bool fitsVersion(int version, int byteLength)
{
    const int dataCodewords = kTotalCodewords[version - 1]
                              - kEccCodewords[version - 1] * kBlocks[version - 1];
    const int capacityBits = dataCodewords * 8;
    const int needed = 4 + charCountBits(version) + byteLength * 8;
    return needed <= capacityBits;
}

int chooseVersion(int byteLength)
{
    for (int version = 1; version <= 10; ++version) {
        if (fitsVersion(version, byteLength))
            return version;
    }
    return -1;
}

std::vector<unsigned char> buildDataCodewords(const QByteArray &payload, int version)
{
    const int dataCodewords = kTotalCodewords[version - 1]
                              - kEccCodewords[version - 1] * kBlocks[version - 1];
    BitBuffer buffer;
    buffer.appendBits(0x4, 4); // byte mode
    buffer.appendBits(static_cast<unsigned int>(payload.size()), charCountBits(version));
    buffer.appendBytes(payload);

    const int capacityBits = dataCodewords * 8;
    const int terminator = std::min(4, capacityBits - buffer.size());
    buffer.appendBits(0, terminator);
    while (buffer.size() % 8 != 0)
        buffer.appendBits(0, 1);

    std::vector<unsigned char> data = buffer.toCodewords();
    data.resize(static_cast<size_t>(dataCodewords), 0);
    for (size_t i = buffer.size() / 8, pad = 0; i < data.size(); ++i, ++pad)
        data[i] = static_cast<unsigned char>(pad % 2 == 0 ? 0xEC : 0x11);
    return data;
}

std::vector<unsigned char> interleave(const std::vector<unsigned char> &data, int version)
{
    const int blocks = kBlocks[version - 1];
    const int eccLength = kEccCodewords[version - 1];
    const int shortLength = static_cast<int>(data.size()) / blocks;
    const int longBlocks = static_cast<int>(data.size()) % blocks;
    const int shortBlocks = blocks - longBlocks;

    std::vector<std::vector<unsigned char>> dataBlocks;
    std::vector<std::vector<unsigned char>> eccBlocks;
    const std::vector<unsigned char> divisor = ReedSolomon::divisor(eccLength);

    size_t offset = 0;
    for (int i = 0; i < blocks; ++i) {
        const int length = shortLength + (i >= shortBlocks ? 1 : 0);
        std::vector<unsigned char> block(data.begin() + static_cast<long>(offset),
                                         data.begin() + static_cast<long>(offset + length));
        offset += static_cast<size_t>(length);
        eccBlocks.push_back(ReedSolomon::remainder(block, divisor));
        dataBlocks.push_back(std::move(block));
    }

    std::vector<unsigned char> result;
    const int maxData = shortLength + (longBlocks > 0 ? 1 : 0);
    for (int i = 0; i < maxData; ++i) {
        for (const auto &block : dataBlocks) {
            if (i < static_cast<int>(block.size()))
                result.push_back(block[static_cast<size_t>(i)]);
        }
    }
    for (int i = 0; i < eccLength; ++i) {
        for (const auto &block : eccBlocks)
            result.push_back(block[static_cast<size_t>(i)]);
    }
    return result;
}

void drawFinder(Matrix &matrix, int centerX, int centerY)
{
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const int distance = std::max(std::abs(dx), std::abs(dy));
            matrix.setFunction(centerX + dx, centerY + dy, distance != 2 && distance != 4);
        }
    }
}

void drawAlignment(Matrix &matrix, int centerX, int centerY)
{
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int distance = std::max(std::abs(dx), std::abs(dy));
            matrix.setFunction(centerX + dx, centerY + dy, distance != 1);
        }
    }
}

void drawFormatBits(Matrix &matrix, int mask)
{
    const int data = (0x00 << 3) | mask; // ECC level M = 0b00
    int remainder = data;
    for (int i = 0; i < 10; ++i)
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    const int bits = ((data << 10) | remainder) ^ 0x5412;

    const int size = matrix.size;
    auto bit = [bits](int index) { return ((bits >> index) & 1) != 0; };

    for (int i = 0; i <= 5; ++i)
        matrix.setFunction(8, i, bit(i));
    matrix.setFunction(8, 7, bit(6));
    matrix.setFunction(8, 8, bit(7));
    matrix.setFunction(7, 8, bit(8));
    for (int i = 9; i < 15; ++i)
        matrix.setFunction(14 - i, 8, bit(i));

    for (int i = 0; i < 8; ++i)
        matrix.setFunction(size - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i)
        matrix.setFunction(8, size - 15 + i, bit(i));
    matrix.setFunction(8, size - 8, true);
}

void drawVersionBits(Matrix &matrix, int version)
{
    if (version < 7)
        return;
    int remainder = version;
    for (int i = 0; i < 12; ++i)
        remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
    const int bits = (version << 12) | remainder;
    for (int i = 0; i < 18; ++i) {
        const bool bit = ((bits >> i) & 1) != 0;
        const int a = matrix.size - 11 + i % 3;
        const int b = i / 3;
        matrix.setFunction(a, b, bit);
        matrix.setFunction(b, a, bit);
    }
}

bool maskBit(int mask, int x, int y)
{
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
    return false;
}

void applyMask(Matrix &matrix, int mask)
{
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size; ++x) {
            if (!matrix.isFunction(x, y) && maskBit(mask, x, y))
                matrix.at(x, y) ^= 1;
        }
    }
}

int penaltyScore(const Matrix &matrix)
{
    const int size = matrix.size;
    int score = 0;

    // Rule 1: runs of five or more identical modules.
    for (int y = 0; y < size; ++y) {
        int run = 1;
        for (int x = 1; x < size; ++x) {
            if (matrix.at(x, y) == matrix.at(x - 1, y)) {
                ++run;
            } else {
                if (run >= 5)
                    score += 3 + (run - 5);
                run = 1;
            }
        }
        if (run >= 5)
            score += 3 + (run - 5);
    }
    for (int x = 0; x < size; ++x) {
        int run = 1;
        for (int y = 1; y < size; ++y) {
            if (matrix.at(x, y) == matrix.at(x, y - 1)) {
                ++run;
            } else {
                if (run >= 5)
                    score += 3 + (run - 5);
                run = 1;
            }
        }
        if (run >= 5)
            score += 3 + (run - 5);
    }

    // Rule 2: 2x2 blocks.
    for (int y = 0; y < size - 1; ++y) {
        for (int x = 0; x < size - 1; ++x) {
            const unsigned char value = matrix.at(x, y);
            if (value == matrix.at(x + 1, y) && value == matrix.at(x, y + 1)
                && value == matrix.at(x + 1, y + 1)) {
                score += 3;
            }
        }
    }

    // Rule 3: finder-like patterns.
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x + 6 < size; ++x) {
            if (matrix.at(x, y) && !matrix.at(x + 1, y) && matrix.at(x + 2, y)
                && matrix.at(x + 3, y) && matrix.at(x + 4, y) && !matrix.at(x + 5, y)
                && matrix.at(x + 6, y)) {
                const bool left = x >= 4 && !matrix.at(x - 1, y) && !matrix.at(x - 2, y)
                                  && !matrix.at(x - 3, y) && !matrix.at(x - 4, y);
                const bool right = x + 10 < size && !matrix.at(x + 7, y) && !matrix.at(x + 8, y)
                                   && !matrix.at(x + 9, y) && !matrix.at(x + 10, y);
                if (left || right)
                    score += 40;
            }
        }
    }
    for (int x = 0; x < size; ++x) {
        for (int y = 0; y + 6 < size; ++y) {
            if (matrix.at(x, y) && !matrix.at(x, y + 1) && matrix.at(x, y + 2)
                && matrix.at(x, y + 3) && matrix.at(x, y + 4) && !matrix.at(x, y + 5)
                && matrix.at(x, y + 6)) {
                const bool top = y >= 4 && !matrix.at(x, y - 1) && !matrix.at(x, y - 2)
                                 && !matrix.at(x, y - 3) && !matrix.at(x, y - 4);
                const bool bottom = y + 10 < size && !matrix.at(x, y + 7) && !matrix.at(x, y + 8)
                                    && !matrix.at(x, y + 9) && !matrix.at(x, y + 10);
                if (top || bottom)
                    score += 40;
            }
        }
    }

    // Rule 4: balance of dark modules.
    int dark = 0;
    for (unsigned char value : matrix.modules)
        dark += value ? 1 : 0;
    const int total = size * size;
    const int k = std::abs(dark * 20 - total * 10) / total;
    score += k * 10;
    return score;
}

Matrix buildMatrix(const QByteArray &payload)
{
    const int version = chooseVersion(payload.size());
    if (version < 0)
        return Matrix();

    const std::vector<unsigned char> data = buildDataCodewords(payload, version);
    const std::vector<unsigned char> codewords = interleave(data, version);

    const int size = version * 4 + 17;
    Matrix matrix(size);

    // Timing patterns
    for (int i = 0; i < size; ++i) {
        matrix.setFunction(6, i, i % 2 == 0);
        matrix.setFunction(i, 6, i % 2 == 0);
    }

    // Finder patterns and separators
    drawFinder(matrix, 3, 3);
    drawFinder(matrix, size - 4, 3);
    drawFinder(matrix, 3, size - 4);

    // Alignment patterns
    const int *alignment = kAlignment[version - 1];
    const int alignmentCount = alignment[2] == 0 ? (alignment[1] == 0 ? 0 : 2)
                                                 : (alignment[3] == 0 ? 3 : 4);
    for (int i = 0; i < alignmentCount; ++i) {
        for (int j = 0; j < alignmentCount; ++j) {
            const bool corner = (i == 0 && j == 0)
                                || (i == 0 && j == alignmentCount - 1)
                                || (i == alignmentCount - 1 && j == 0);
            if (!corner)
                drawAlignment(matrix, alignment[i], alignment[j]);
        }
    }

    // Reserve format and version areas.
    drawFormatBits(matrix, 0);
    drawVersionBits(matrix, version);

    // Place data bits.
    BitBuffer bits;
    for (unsigned char codeword : codewords)
        bits.appendBits(codeword, 8);

    int bitIndex = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vertical = 0; vertical < size; ++vertical) {
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? size - 1 - vertical : vertical;
                if (!matrix.isFunction(x, y) && bitIndex < bits.size()) {
                    matrix.at(x, y) = bits.bit(bitIndex) ? 1 : 0;
                    ++bitIndex;
                }
            }
        }
    }

    // Choose the mask with the lowest penalty.
    int bestMask = 0;
    int bestScore = INT_MAX;
    Matrix best;
    for (int mask = 0; mask < 8; ++mask) {
        Matrix candidate = matrix;
        applyMask(candidate, mask);
        drawFormatBits(candidate, mask);
        const int score = penaltyScore(candidate);
        if (score < bestScore) {
            bestScore = score;
            bestMask = mask;
            best = std::move(candidate);
        }
    }

    // The winning candidate already has its format bits.
    Q_UNUSED(bestMask)
    return best;
}

} // namespace

QImage QrCode::encode(const QString &text, int moduleSize, int borderModules,
                      const QColor &foreground, const QColor &background)
{
    const QByteArray payload = text.toUtf8();
    if (payload.isEmpty())
        return QImage();

    Matrix matrix = buildMatrix(payload);
    if (matrix.size <= 0)
        return QImage();

    moduleSize = std::max(1, moduleSize);
    borderModules = std::max(0, borderModules);
    const int imageSize = (matrix.size + borderModules * 2) * moduleSize;

    QImage image(imageSize, imageSize, QImage::Format_ARGB32);
    image.fill(background);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(foreground);
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size; ++x) {
            if (!matrix.at(x, y))
                continue;
            painter.drawRect((x + borderModules) * moduleSize,
                             (y + borderModules) * moduleSize,
                             moduleSize, moduleSize);
        }
    }
    painter.end();
    return image;
}

} // namespace lightrec
