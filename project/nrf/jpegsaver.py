import re

with open('Grayscale_image.txt', 'r') as f:
    text = f.read()

#Remove all whitespace and unnecessary formatting from original textfile
text_no_spaces = re.sub(r'[ \t\r\n]+', '', text)
text_no_spaces = re.sub(r',+', ',', text_no_spaces)

#Save cleaned CSV
with open('Grayscale_image_clean.csv', 'w') as f:
    f.write(text_no_spaces)

print("Cleaned CSV saved as Grayscale_image_clean.csv")