import math
from typing import Optional, List

class AvatarGenerator:
    DEFAULT_COLORS = ["#000000", "#8f1414", "#e50e0e", "#f3450f", "#fcac03"]
    DEFAULT_SIZE = 80
    DEFAULT_VARIANT = 'marble'
    VALID_VARIANTS = {'marble', 'beam', 'pixel', 'sunset', 'ring', 'bauhaus'}

    def __init__(self, default_colors: Optional[List[str]] = None, default_size: Optional[int] = None, default_variant: Optional[str] = None):
        self.colors = default_colors if default_colors else self.DEFAULT_COLORS
        self.size = default_size if default_size else self.DEFAULT_SIZE
        self.variant = default_variant if default_variant else self.DEFAULT_VARIANT
        
        self.AVATAR_GENERATORS = {
            'marble': self._generate_marble_avatar,
            'beam': self._generate_beam_avatar,
            'pixel': self._generate_pixel_avatar,
            'sunset': self._generate_sunset_avatar,
            'ring': self._generate_ring_avatar,
            'bauhaus': self._generate_bauhaus_avatar,
        }

    @staticmethod
    def _hash_code(name: str) -> int:
        """Generate hash from name string - exact port of hashCode function"""
        try:
            if not name or not isinstance(name, str):
                # Consider raising a TypeError or returning a default hash for invalid input
                return 12345 # Fallback for invalid input
            hash_val = 0
            for char in name:
                hash_val = (hash_val << 5) - hash_val + ord(char)
                hash_val &= hash_val # Convert to 32bit integer
            return abs(hash_val)
        except Exception:
            return 12345  # fallback hash value

    @staticmethod
    def _get_modulus(num: int, max_val: int) -> int:
        """Get modulus"""
        return num % max_val

    @staticmethod
    def _get_digit(number: int, ntn: int) -> int:
        """Get digit at position"""
        try:
            if not isinstance(number, int) or not isinstance(ntn, int):
                return 0 # Fallback for invalid input type
            return int((number / (10 ** ntn)) % 10)
        except (ZeroDivisionError, ValueError, OverflowError):
            return 0

    @staticmethod
    def _get_boolean(number: int, ntn: int) -> bool:
        """Get boolean from digit"""
        return not ((AvatarGenerator._get_digit(number, ntn)) % 2)

    @staticmethod
    def _get_angle(x: float, y: float) -> float:
        """Get angle from coordinates"""
        return math.atan2(y, x) * 180 / math.pi

    @staticmethod
    def _get_unit(number: int, range_val: int, index: int = None) -> int:
        """Get unit value with optional negative"""
        try:
            if not isinstance(number, int) or not isinstance(range_val, int) or range_val == 0:
                return 0 # Fallback for invalid input
            value = number % range_val
            if index and ((AvatarGenerator._get_digit(number, index) % 2) == 0):
                return -value
            return value
        except (ZeroDivisionError, ValueError, TypeError):
            return 0

    @staticmethod
    def _get_random_color(number: int, colors: List[str], range_val: int) -> str:
        """Get random color from palette"""
        try:
            if not colors or range_val == 0:
                return AvatarGenerator.DEFAULT_COLORS[0] # Fallback
            return colors[number % range_val]
        except (IndexError, TypeError, ZeroDivisionError):
            return AvatarGenerator.DEFAULT_COLORS[0]

    @staticmethod
    def _get_contrast(hexcolor: str) -> str:
        """Get contrasting color (black or white)"""
        try:
            if not hexcolor or not isinstance(hexcolor, str):
                return '#000000' # Fallback
            
            # Remove leading # if present
            if hexcolor.startswith('#'):
                hexcolor = hexcolor[1:]
            
            # Clean up any remaining quotes or whitespace
            hexcolor = hexcolor.strip().replace('"', '').replace("'", "")
            
            # Ensure we have exactly 6 hex characters
            if len(hexcolor) != 6:
                return '#000000' # Fallback
            
            # Validate hex characters
            try:
                int(hexcolor, 16)
            except ValueError:
                return '#000000' # Fallback
            
            # Convert to RGB
            r = int(hexcolor[0:2], 16)
            g = int(hexcolor[2:4], 16)
            b = int(hexcolor[4:6], 16)
            
            # Get YIQ ratio
            yiq = ((r * 299) + (g * 587) + (b * 114)) / 1000
            
            # Check contrast
            return '#000000' if yiq >= 128 else '#FFFFFF'
        except Exception:
            return '#000000'

    @staticmethod
    def normalize_colors(colors_param: Optional[str]) -> List[str]:
        """Normalize color palette from query parameter"""
        try:
            if not colors_param:
                return AvatarGenerator.DEFAULT_COLORS
            
            # Clean up URL encoding
            cleaned = colors_param.replace('%22', '"').replace('%20', ' ').replace('%5B', '[').replace('%5D', ']').strip()
            
            # Extract colors from different formats
            color_list = AvatarGenerator._extract_color_strings(cleaned)
            
            # Validate and normalize colors
            return AvatarGenerator._validate_color_list(color_list)
            
        except Exception:
            return AvatarGenerator.DEFAULT_COLORS

    @staticmethod
    def _extract_color_strings(cleaned_colors: str) -> List[str]:
        """Extract color strings from cleaned input"""
        # Handle array format: ["#xxxxxx", "#xxxxxx", ...]
        if cleaned_colors.startswith('[') and cleaned_colors.endswith(']'):
            inner_content = cleaned_colors[1:-1].strip()
            if not inner_content:
                return []
            return [part.strip().replace('"', '').replace("'", '') for part in inner_content.split(',')]
        
        # Handle comma-separated format: #xxxxxx, #xxxxxx, ...
        return [part.strip().replace('"', '').replace("'", '') for part in cleaned_colors.split(',')]

    @staticmethod
    def _validate_color_list(color_list: List[str]) -> List[str]:
        """Validate and normalize hex colors"""
        normalized_colors = []
        
        for color in color_list:
            color = color.strip()
            if not color:
                continue
            if not color.startswith('#'):
                color = '#' + color
            if AvatarGenerator._is_valid_hex_color(color):
                normalized_colors.append(color)
        
        return normalized_colors if normalized_colors else AvatarGenerator.DEFAULT_COLORS

    @staticmethod
    def _is_valid_hex_color(color: str) -> bool:
        """Check if string is valid hex color"""
        return len(color) == 7 and all(c in '0123456789ABCDEFabcdef' for c in color[1:])

    def _generate_marble_avatar(self, name: str, colors: List[str], size: int, square: bool, title: bool) -> str:
        """Generate marble variant avatar"""
        ELEMENTS = 3
        SIZE = 80 # Intrinsic size of the SVG design
        
        def generate_colors_marble(name: str, current_colors: List[str]):
            num_from_name = self._hash_code(name)
            range_val = len(current_colors) if current_colors else len(self.DEFAULT_COLORS)
            
            elements_properties = []
            for i in range(ELEMENTS):
                elements_properties.append({
                    'color': self._get_random_color(num_from_name + i, current_colors, range_val),
                    'translateX': self._get_unit(num_from_name * (i + 1), SIZE // 10, 1),
                    'translateY': self._get_unit(num_from_name * (i + 1), SIZE // 10, 2),
                    'scale': 1.2 + self._get_unit(num_from_name * (i + 1), SIZE // 20) / 10,
                    'rotate': self._get_unit(num_from_name * (i + 1), 360, 1)
                })
            
            return elements_properties
        
        properties = generate_colors_marble(name, colors)
        mask_id = f"mask_{self._hash_code(name)}"
        filter_id = f"filter_{self._hash_code(name)}"
        
        # Use the passed 'size' for width/height, internal 'SIZE' for viewBox
        svg = f'''<svg viewBox="0 0 {SIZE} {SIZE}" fill="none" role="img" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">'''
        
        if title:
            svg += f'<title>{name}</title>'
        
        svg += f'''
    <mask id="{mask_id}" maskUnits="userSpaceOnUse" x="0" y="0" width="{SIZE}" height="{SIZE}">
        <rect width="{SIZE}" height="{SIZE}" {"" if square else f'rx="{SIZE * 2}"'} fill="#FFFFFF" />
    </mask>
    <g mask="url(#{mask_id})">
        <rect width="{SIZE}" height="{SIZE}" fill="{properties[0]['color']}" />
        <path
            filter="url(#{filter_id})"
            d="M32.414 59.35L50.376 70.5H72.5v-71H33.728L26.5 13.381l19.057 27.08L32.414 59.35z"
            fill="{properties[1]['color']}"
            transform="translate({properties[1]['translateX']} {properties[1]['translateY']}) rotate({properties[1]['rotate']} {SIZE // 2} {SIZE // 2}) scale({properties[1]['scale']})"
        />
        <path
            filter="url(#{filter_id})"
            style="mix-blend-mode: overlay;"
            d="M22.216 24L0 46.75l14.108 38.129L78 86l-3.081-59.276-22.378 4.005 12.972 20.186-23.35 27.395L22.215 24z"
            fill="{properties[2]['color']}"
            transform="translate({properties[2]['translateX']} {properties[2]['translateY']}) rotate({properties[2]['rotate']} {SIZE // 2} {SIZE // 2}) scale({properties[2]['scale']})"
        />
    </g>
    <defs>
        <filter
            id="{filter_id}"
            filterUnits="userSpaceOnUse"
            colorInterpolationFilters="sRGB"
        >
            <feFlood floodOpacity="0" result="BackgroundImageFix" />
            <feBlend in="SourceGraphic" in2="BackgroundImageFix" result="shape" />
            <feGaussianBlur stdDeviation="7" result="effect1_foregroundBlur" />
        </filter>
    </defs>
</svg>'''
        
        return svg

    def _generate_beam_avatar(self, name: str, colors: List[str], size: int, square: bool, title: bool) -> str:
        """Generate beam variant avatar"""
        SIZE = 36 # Intrinsic size of the SVG design
        
        def generate_data_beam(name: str, current_colors: List[str]):
            num_from_name = self._hash_code(name)
            range_val = len(current_colors) if current_colors else len(self.DEFAULT_COLORS)
            wrapper_color = self._get_random_color(num_from_name, current_colors, range_val)
            pre_translate_x = self._get_unit(num_from_name, 10, 1)
            wrapper_translate_x = pre_translate_x + SIZE // 9 if pre_translate_x < 5 else pre_translate_x
            pre_translate_y = self._get_unit(num_from_name, 10, 2)
            wrapper_translate_y = pre_translate_y + SIZE // 9 if pre_translate_y < 5 else pre_translate_y
            
            data = {
                'wrapperColor': wrapper_color,
                'faceColor': self._get_contrast(wrapper_color),
                'backgroundColor': self._get_random_color(num_from_name + 13, current_colors, range_val),
                'wrapperTranslateX': wrapper_translate_x,
                'wrapperTranslateY': wrapper_translate_y,
                'wrapperRotate': self._get_unit(num_from_name, 360),
                'wrapperScale': 1 + self._get_unit(num_from_name, SIZE // 12) / 10,
                'isMouthOpen': self._get_boolean(num_from_name, 2),
                'isCircle': self._get_boolean(num_from_name, 1),
                'eyeSpread': self._get_unit(num_from_name, 5),
                'mouthSpread': self._get_unit(num_from_name, 3),
                'faceRotate': self._get_unit(num_from_name, 10, 3),
                'faceTranslateX': wrapper_translate_x // 2 if wrapper_translate_x > SIZE // 6 else self._get_unit(num_from_name, 8, 1),
                'faceTranslateY': wrapper_translate_y // 2 if wrapper_translate_y > SIZE // 6 else self._get_unit(num_from_name, 7, 2),
            }
            
            return data
        
        data = generate_data_beam(name, colors)
        mask_id = f"mask_{self._hash_code(name)}"
        
        svg = f'''<svg viewBox="0 0 {SIZE} {SIZE}" fill="none" role="img" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">'''
        
        if title:
            svg += f'<title>{name}</title>'
        
        rx_value = SIZE if data['isCircle'] else SIZE // 6
        
        svg += f'''
    <mask id="{mask_id}" maskUnits="userSpaceOnUse" x="0" y="0" width="{SIZE}" height="{SIZE}">
        <rect width="{SIZE}" height="{SIZE}" {"" if square else f'rx="{SIZE * 2}"'} fill="#FFFFFF" />
    </mask>
    <g mask="url(#{mask_id})">
        <rect width="{SIZE}" height="{SIZE}" fill="{data['backgroundColor']}" />
        <rect
            x="0"
            y="0"
            width="{SIZE}"
            height="{SIZE}"
            transform="translate({data['wrapperTranslateX']} {data['wrapperTranslateY']}) rotate({data['wrapperRotate']} {SIZE // 2} {SIZE // 2}) scale({data['wrapperScale']})"
            fill="{data['wrapperColor']}"
            rx="{rx_value}"
        />
        <g transform="translate({data['faceTranslateX']} {data['faceTranslateY']}) rotate({data['faceRotate']} {SIZE // 2} {SIZE // 2})">'''
        
        if data['isMouthOpen']:
            svg += f'''
            <path
                d="M15 {19 + data['mouthSpread']}c2 1 4 1 6 0"
                stroke="{data['faceColor']}"
                fill="none"
                strokeLinecap="round"
            />'''
        else:
            svg += f'''
            <path
                d="M13,{19 + data['mouthSpread']} a1,0.75 0 0,0 10,0"
                fill="{data['faceColor']}"
            />'''
        
        svg += f'''
            <rect
                x="{14 - data['eyeSpread']}"
                y="14"
                width="1.5"
                height="2"
                rx="1"
                stroke="none"
                fill="{data['faceColor']}"
            />
            <rect
                x="{20 + data['eyeSpread']}"
                y="14"
                width="1.5"
                height="2"
                rx="1"
                stroke="none"
                fill="{data['faceColor']}"
            />
        </g>
    </g>
</svg>'''
        
        return svg

    def _generate_pixel_avatar(self, name: str, colors: List[str], size: int, square: bool, title: bool) -> str:
        """Generate pixel variant avatar"""
        ELEMENTS = 64 # 8x8 grid
        SIZE = 80 # Intrinsic SVG size

        def generate_colors_pixel(name: str, current_colors: List[str]):
            num_from_name = self._hash_code(name)
            # Use current_colors if provided, otherwise fallback to instance default, then class default
            final_colors = current_colors if current_colors else self.colors
            range_val = len(final_colors)
            
            color_list = []
            for i in range(ELEMENTS):
                color_list.append(self._get_random_color(num_from_name + i, final_colors, range_val))
            return color_list
        
        pixel_colors = generate_colors_pixel(name, colors)
        mask_id = f"mask_{self._hash_code(name)}"
        
        svg = f'''<svg viewBox="0 0 {SIZE} {SIZE}" fill="none" role="img" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">'''
        
        if title:
            svg += f'<title>{name}</title>'
        
        svg += f'''
    <mask id="{mask_id}" mask-type="alpha" maskUnits="userSpaceOnUse" x="0" y="0" width="{SIZE}" height="{SIZE}">
        <rect width="{SIZE}" height="{SIZE}" {"" if square else f'rx="{SIZE * 2}"'} fill="#FFFFFF" />
    </mask>
    <g mask="url(#{mask_id})">'''
        
        # Generate 8x8 grid of pixels
        idx = 0
        pixel_size = SIZE // 8 # Each pixel is 10x10 if SIZE is 80
        for row in range(8):
            for col in range(8):
                svg += f'''
        <rect x="{col * pixel_size}" y="{row * pixel_size}" width="{pixel_size}" height="{pixel_size}" fill="{pixel_colors[idx]}" />'''
                idx +=1
        
        svg += '''
    </g>
</svg>'''
        
        return svg

    def _generate_sunset_avatar(self, name: str, colors: List[str], size: int, square: bool, title: bool) -> str:
        """Generate sunset variant avatar"""
        ELEMENTS = 4 # For 4 gradient stops
        SIZE = 80 # Intrinsic SVG size
        
        def generate_colors_sunset(name: str, current_colors: List[str]):
            num_from_name = self._hash_code(name)
            # Use current_colors if provided, otherwise fallback to instance default, then class default
            final_colors = current_colors if current_colors else self.colors
            # Ensure we have at least 4 colors for sunset, repeat if necessary
            if len(final_colors) < ELEMENTS:
                final_colors = (final_colors * (ELEMENTS // len(final_colors) + 1))[:ELEMENTS]

            range_val = len(final_colors)
            
            color_list = []
            for i in range(ELEMENTS):
                color_list.append(self._get_random_color(num_from_name + i, final_colors, range_val))
            return color_list
        
        sunset_colors = generate_colors_sunset(name, colors)
        # name_without_space = name.replace(' ', '').replace('-', '').replace('_', '') # Not used
        hash_val = abs(self._hash_code(name))
        mask_id = f"mask_{hash_val}"
        gradient_id_1 = f"gradient_paint0_linear_{hash_val}"
        gradient_id_2 = f"gradient_paint1_linear_{hash_val}"
        
        rx_value = '' if square else f'rx="{SIZE * 2}"'
        
        svg = f'''<svg viewBox="0 0 {SIZE} {SIZE}" fill="none" role="img" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">'''
        
        if title:
            svg += f'<title>{name}</title>'
        
        svg += f'''
    <defs>
        <linearGradient
            id="{gradient_id_1}"
            x1="{SIZE // 2}"
            y1="0"
            x2="{SIZE // 2}"
            y2="{SIZE // 2}"
            gradientUnits="userSpaceOnUse"
        >
            <stop stop-color="{sunset_colors[0]}" />
            <stop offset="1" stop-color="{sunset_colors[1]}" />
        </linearGradient>
        <linearGradient
            id="{gradient_id_2}"
            x1="{SIZE // 2}"
            y1="{SIZE // 2}"
            x2="{SIZE // 2}"
            y2="{SIZE}"
            gradientUnits="userSpaceOnUse"
        >
            <stop stop-color="{sunset_colors[2]}" />
            <stop offset="1" stop-color="{sunset_colors[3]}" />
        </linearGradient>
    </defs>
    <mask id="{mask_id}" maskUnits="userSpaceOnUse" x="0" y="0" width="{SIZE}" height="{SIZE}">
        <rect width="{SIZE}" height="{SIZE}" {rx_value} fill="#FFFFFF" />
    </mask>
    <g mask="url(#{mask_id})">
        <path fill="url(#{gradient_id_1})" d="M0 0h{SIZE}v{SIZE//2}H0z" />
        <path fill="url(#{gradient_id_2})" d="M0 {SIZE//2}h{SIZE}v{SIZE//2}H0z" />
    </g>
</svg>'''
        
        return svg

    def _generate_ring_avatar(self, name: str, colors: List[str], size: int, square: bool, title: bool) -> str:
        """Generate ring variant avatar"""
        SIZE = 90 # Intrinsic SVG size
        COLORS_NEEDED = 9 # Number of colors used in the SVG paths
        
        def generate_colors_ring(name: str, current_colors: List[str]):
            num_from_name = self._hash_code(name)
            # Use current_colors if provided, otherwise fallback to instance default, then class default
            final_colors = current_colors if current_colors else self.colors
            # Ensure we have enough colors, repeat if necessary
            if len(final_colors) < COLORS_NEEDED:
                 final_colors = (final_colors * (COLORS_NEEDED // len(final_colors) + 1))[:COLORS_NEEDED]
            
            range_val = len(final_colors)
            
            ring_palette = []
            for i in range(COLORS_NEEDED):
                ring_palette.append(self._get_random_color(num_from_name + i, final_colors, range_val))
            return ring_palette
        
        ring_colors = generate_colors_ring(name, colors)
        mask_id = f"mask_{self._hash_code(name)}"
        
        svg = f'''<svg viewBox="0 0 {SIZE} {SIZE}" fill="none" role="img" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">'''
        
        if title:
            svg += f'<title>{name}</title>'
        
        svg += f'''
    <mask id="{mask_id}" maskUnits="userSpaceOnUse" x="0" y="0" width="{SIZE}" height="{SIZE}">
        <rect width="{SIZE}" height="{SIZE}" {"" if square else f'rx="{SIZE * 2}"'} fill="#FFFFFF" />
    </mask>
    <g mask="url(#{mask_id})">
        <path d="M0 0h{SIZE}v{SIZE//2}H0z" fill="{ring_colors[0]}" />
        <path d="M0 {SIZE//2}h{SIZE}v{SIZE//2}H0z" fill="{ring_colors[1]}" />
        <path d="M{SIZE-7} {SIZE//2}a{SIZE//2-2} {SIZE//2-2} 0 00-{SIZE-14} 0h{SIZE-14}z" fill="{ring_colors[2]}" /> 
        <path d="M{SIZE-7} {SIZE//2}a{SIZE//2-2} {SIZE//2-2} 0 01-{SIZE-14} 0h{SIZE-14}z" fill="{ring_colors[3]}" />
        <path d="M{SIZE-13} {SIZE//2}a{SIZE//2-8} {SIZE//2-8} 0 10-{SIZE-26} 0h{SIZE-26}z" fill="{ring_colors[4]}" />
        <path d="M{SIZE-13} {SIZE//2}a{SIZE//2-8} {SIZE//2-8} 0 11-{SIZE-26} 0h{SIZE-26}z" fill="{ring_colors[5]}" />
        <path d="M{SIZE-19} {SIZE//2}a{SIZE//2-14} {SIZE//2-14} 0 00-{SIZE-38} 0h{SIZE-38}z" fill="{ring_colors[6]}" />
        <path d="M{SIZE-19} {SIZE//2}a{SIZE//2-14} {SIZE//2-14} 0 01-{SIZE-38} 0h{SIZE-38}z" fill="{ring_colors[7]}" />
        <circle cx="{SIZE//2}" cy="{SIZE//2}" r="{SIZE//2-22}" fill="{ring_colors[8]}" />
    </g>
</svg>'''
        
        return svg

    def _generate_bauhaus_avatar(self, name: str, colors: List[str], size: int, square: bool, title: bool) -> str:
        """Generate bauhaus variant avatar"""
        ELEMENTS = 4 # Number of geometric elements
        SIZE = 80 # Intrinsic SVG size
        
        def generate_colors_bauhaus(name: str, current_colors: List[str]):
            num_from_name = self._hash_code(name)
            # Use current_colors if provided, otherwise fallback to instance default, then class default
            final_colors = current_colors if current_colors else self.colors
            range_val = len(final_colors)

            properties = []
            for i in range(ELEMENTS):
                properties.append({
                    'color': self._get_random_color(num_from_name + i, final_colors, range_val),
                    'translateX': self._get_unit(num_from_name * (i + 1), SIZE // 2 - (i + 17), 1),
                    'translateY': self._get_unit(num_from_name * (i + 1), SIZE // 2 - (i + 17), 2),
                    'rotate': self._get_unit(num_from_name * (i + 1), 360),
                    'isSquare': self._get_boolean(num_from_name, 2)
                })
            return properties
        
        properties = generate_colors_bauhaus(name, colors)
        mask_id = f"mask_{self._hash_code(name)}"
        
        svg = f'''<svg viewBox="0 0 {SIZE} {SIZE}" fill="none" role="img" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">'''
        
        if title:
            svg += f'<title>{name}</title>'
        
        svg += f'''
    <mask id="{mask_id}" maskUnits="userSpaceOnUse" x="0" y="0" width="{SIZE}" height="{SIZE}">
        <rect width="{SIZE}" height="{SIZE}" {"" if square else f'rx="{SIZE * 2}"'} fill="#FFFFFF" />
    </mask>
    <g mask="url(#{mask_id})">
        <rect width="{SIZE}" height="{SIZE}" fill="{properties[0]['color']}" />
        <rect
            x="{(SIZE - 60) // 2}"
            y="{(SIZE - 20) // 2}"
            width="{SIZE}" 
            height="{SIZE if properties[1]['isSquare'] else SIZE // 8}"
            fill="{properties[1]['color']}"
            transform="translate({properties[1]['translateX']} {properties[1]['translateY']}) rotate({properties[1]['rotate']} {SIZE // 2} {SIZE // 2})"
        />
        <circle
            cx="{SIZE // 2}"
            cy="{SIZE // 2}"
            fill="{properties[2]['color']}"
            r="{SIZE // 5}"
            transform="translate({properties[2]['translateX']} {properties[2]['translateY']})"
        />
        <line
            x1="0"
            y1="{SIZE // 2}"
            x2="{SIZE}"
            y2="{SIZE // 2}"
            strokeWidth="2"
            stroke="{properties[3]['color']}"
            transform="translate({properties[3]['translateX']} {properties[3]['translateY']}) rotate({properties[3]['rotate']} {SIZE // 2} {SIZE // 2})"
        />
    </g>
</svg>'''
        
        return svg

    def generate_avatar(self, 
                        name: str, 
                        variant: Optional[str] = None, 
                        colors: Optional[List[str]] = None, # Allow passing colors as list of strings
                        colors_param: Optional[str] = None, # Allow passing colors as a string parameter
                        size: Optional[int] = None, 
                        square: bool = False, 
                        title: bool = True) -> str:
        """
        Generate an SVG avatar.

        Args:
            name: The name to generate the avatar for.
            variant: The avatar variant (e.g., 'marble', 'beam'). Uses instance default if None.
            colors: A list of hex color strings. Overrides colors_param if provided.
            colors_param: A string representation of colors (e.g., "['#FF0000', '#00FF00']" or "#FF0000,#00FF00").
                          Used if 'colors' list is not provided.
            size: The desired size of the avatar in pixels. Uses instance default if None.
            square: If True, generates a square avatar. Otherwise, a circular one.
            title: If True, includes a <title> tag with the name in the SVG.

        Returns:
            An SVG string representing the avatar.
        """
        current_variant = variant if variant and variant in self.VALID_VARIANTS else self.variant
        current_size = size if size else self.size
        
        # Determine colors: prioritize direct list, then string param, then instance default
        if colors:
            # Validate and normalize if a list is directly passed
            final_colors = self._validate_color_list(colors)
        elif colors_param:
            final_colors = self.normalize_colors(colors_param)
        else:
            final_colors = self.colors # Use instance default colors

        if not final_colors: # Ensure there's always a fallback
             final_colors = self.DEFAULT_COLORS


        generator_func = self.AVATAR_GENERATORS.get(current_variant)

        if generator_func:
            return generator_func(name, final_colors, current_size, square, title)
        else:
            # Fallback to default variant if the chosen one is somehow invalid after checks
            default_gen_func = self.AVATAR_GENERATORS.get(self.DEFAULT_VARIANT)
            return default_gen_func(name, final_colors, current_size, square, title)
