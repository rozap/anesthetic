f = File.open('boot_image.data', 'rb')
loop do
    r = f.getbyte
    g = f.getbyte
    b = f.getbyte
    break if r.nil?

    # convert to RGB 5:6:5
    color = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);

    puts "  #{color},"
end
